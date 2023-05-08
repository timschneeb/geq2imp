#include <QCoreApplication>
#include <QCommandLineParser>
#include <QScopeGuard>
#include <QTextStream>
#include <QFile>
#include <QBuffer>

#include <qtcsv/reader.h>
#include <qtcsv/variantdata.h>

extern "C" {
    #include <jdsp_header.h>
    #include "Effects/eel2/numericSys/libsamplerate/samplerate.h"
    #include "Effects/eel2/dr_mp3.h"
    #include "Effects/eel2/dr_flac.h"
    #include "Effects/eel2/dr_wav.h"

    void NodesSorter(ArbitraryEq *arbEq);
}

#define STR_(x) #x
#define STR(x) STR_(x)
#define APP_VERSION_FULL STR(APP_VERSION)

static QTextStream out(stdout);
static QTextStream err(stderr);



bool write_imp_res(const std::string& path, const std::vector<float>& frames, int srate, int channels) {

    drwav pWav;
    drwav_data_format format;
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = channels;
    format.sampleRate = srate;
    format.bitsPerSample = 32;
    unsigned int fail = drwav_init_file_write(&pWav, path.c_str(), &format, 0);
    drwav_uint64 framesWritten = drwav_write_pcm_frames(&pWav, frames.size() / channels, &(frames[0]));
    drwav_uninit(&pWav);

    return fail || framesWritten <= 0;
}

template<typename T>
std::vector<T> interleave_vect(const std::vector<T>& avec, const std::vector<T>& bvec)
{
    assert(avec.size() == bvec.size() && "Left/right frame vectors must be of equal length");

    std::vector<T> result;
    result.reserve(avec.size() + bvec.size());

    for(auto ait = avec.begin(), bit = bvec.begin();
        ait != avec.end() || bit != bvec.end();)
    {
        if(ait != avec.end()) result.push_back(*ait++);
        if(bit != bvec.end()) result.push_back(*bit++);
    }

    return result;
}

bool read_csv(std::vector<float>& frames, const QString& path, JamesDSPLib* jdsp) {

    QtCSV::VariantData variant;
    if(!QtCSV::Reader::readToData(path, variant, "\t"))
    {
        err << "error: failed to read csv file from disk" << Qt::endl;
        return false;
    }

    if(variant.rowCount() <= 1)
    {
        err << "error: csv has no rows and is empty" << Qt::endl;
        return false;
    }

    std::vector<EqNode> nodes_vector(variant.rowCount());
    for (int row = 0; row < variant.rowCount(); row++)
    {
        auto val = variant.rowValues(row);
        if(val.length() < 2)
        {
            err << "warning: skipping row " << row + 1 << " because it contains less than two columns" << Qt::endl;
            continue;
        }

        bool freqValid = false, gainValid = false;
        float freq = val[0].toFloat(&freqValid);
        float gain = val[1].toFloat(&gainValid);

        if(!freqValid || !gainValid) {
            err << "warning: skipping row " << row + 1 << " because it contains invalid data" << Qt::endl;
            continue;
        }

        EqNode node;
        node.freq = freq;
        node.gain = gain;
        nodes_vector.push_back(node);
    }

    if(nodes_vector.empty()) {
        err << "error: no valid data found; the csv file has must exactly two columns per row" << Qt::endl;
        err << "error:\t -> first column: frequency (Hz); second column: gain (dB)" << Qt::endl;
        return false;
    }

    auto* arbEq = &jdsp->arbMag.coeffGen;
    if (arbEq->nodes)
        EqNodesFree(arbEq);

    auto** nodes = (EqNode**)malloc(nodes_vector.size() * sizeof(EqNode*));
    for (size_t i = 0; i < nodes_vector.size(); i++)
    {
        nodes[i] = (EqNode*)malloc(sizeof(EqNode));
        memset(nodes[i], 0, sizeof(EqNode));

        auto copy = nodes_vector[i];
        nodes[i]->freq = copy.freq;
        nodes[i]->gain = copy.gain;
    }
    arbEq->nodesCount = nodes_vector.size();
    arbEq->nodes = nodes;
    NodesSorter(arbEq);

    float *eqFil = jdsp->arbMag.coeffGen.GetFilter(&jdsp->arbMag.coeffGen, (float)jdsp->fs);
    int n = jdsp->arbMag.filterLen;

    frames.clear();
    frames.reserve(nodes_vector.size());
    frames.insert(frames.end(), &eqFil[0], &eqFil[n]);
    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication app( argc, argv );
    app.setApplicationName("gep2imp");
    app.setApplicationVersion(APP_VERSION_FULL);

	QCommandLineParser parser;
    parser.setApplicationDescription("Converts GraphicEQ CSV files to impulse responses.\nCSV files (tab-separated) can be exported using JDSP4Linux or EqualizerAPO.");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption opt_srate(QStringList() << "s" << "srate", "Set target sampling rate (default: 48000Hz)", "srate", "48000");
    QCommandLineOption opt_out(QStringList() << "o" << "output", "Output impulse response file (default: output.wav)", "output-file", "output.wav");
    QCommandLineOption opt_in_left(QStringList() << "l" << "left", "CSV file for left channel", "left-input-file");
    QCommandLineOption opt_in_right(QStringList() << "r" << "right", "CSV file for right channel", "right-input-file");
    QCommandLineOption opt_in_mono(QStringList() << "m" << "mono", "CSV file for both channels", "input-file");

    parser.addOption(opt_srate);
    parser.addOption(opt_out);
    parser.addOption(opt_in_left);
    parser.addOption(opt_in_right);
    parser.addOption(opt_in_mono);

    parser.process(app);

    // Locale workaround for drwav
    QLocale::setDefault(QLocale::c());
    setlocale(LC_NUMERIC, "C");

    if(!parser.isSet(opt_in_left) && !parser.isSet(opt_in_right) && !parser.isSet(opt_in_mono)) {
        parser.showHelp(1);
    }

    bool validSrate = false;
    unsigned int srate = parser.value(opt_srate).toUInt(&validSrate);

    if(!validSrate || srate < 8000) {
        err << "error: invalid sampling rate" << Qt::endl;
        return 1;
    }

    auto* jdsp = new JamesDSPLib;
    JamesDSPGlobalMemoryAllocation();
    JamesDSPInit(jdsp, 128, srate);

    unsigned int channels = 1;
    std::vector<float> final;

    if(parser.isSet(opt_in_left) && parser.isSet(opt_in_right) && !parser.isSet(opt_in_mono)) {
        std::vector<float> left;
        std::vector<float> right;
        bool leftValid = read_csv(left, parser.value(opt_in_left), jdsp);
        if(!leftValid) {
            err << "error: failed to process csv for left channel";
            return 2;
        }

        bool rightValid = read_csv(right, parser.value(opt_in_right), jdsp);
        if(!rightValid) {
            err << "error: failed to process csv for right channel";
            return 2;
        }

        channels = 2;
        final = interleave_vect(left, right);
    }
    else if(!parser.isSet(opt_in_left) && !parser.isSet(opt_in_right) && parser.isSet(opt_in_mono)) {
        bool valid = read_csv(final, parser.value(opt_in_mono), jdsp);
        if(!valid) {
            err << "error: failed to process csv";
            return 2;
        }
    }
    else {
        err << "error: invalid combination of --left, --right, and --mono arguments" << Qt::endl;
        err << "error:\t -> to create a single channel IR, --mono must be used alone" << Qt::endl;
        err << "error:\t -> to create a dual channel IR, --left and --right must always be used together" << Qt::endl;
        return 3;
    }


    auto out_path = parser.value(opt_out);
    if(!write_imp_res(out_path.toStdString(), final, srate, channels)) {
        err << "error: failed to write output file to disk" << Qt::endl;
        return 4;
    }

    out << final.size() << " samples written to '" << out_path << "'" << Qt::endl;

    JamesDSPFree(jdsp);
    JamesDSPGlobalMemoryDeallocation();

    delete jdsp;
    return 0;
}
