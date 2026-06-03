/**
 * DentScanComparePro - Automated Multi-Factor Scanner Evaluation
 *
 * Main entry point with dual-mode support:
 * - GUI mode (default): Interactive ROI template editing + batch runner
 * - CLI mode (--batch): Headless batch processing
 */

#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <iostream>

#include "batch/BatchRunner.h"
#include "config/StudyConfig.h"
#include "config/ROIConfig.h"
#include "gui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("DentScanComparePro");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setOrganizationName("Prof. Dr. Karl-Heinz Kunzelmann");

    // Command-line argument parsing
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Automated batch evaluation of dental intraoral scanner accuracy.\n"
        "Computes ISO 5725/12836-compliant trueness and precision metrics."
    );
    parser.addHelpOption();
    parser.addVersionOption();

    // Batch mode flag
    QCommandLineOption batchOption(
        QStringList() << "b" << "batch",
        "Run in headless batch mode (no GUI)."
    );
    parser.addOption(batchOption);

    // Study configuration file
    QCommandLineOption studyOption(
        QStringList() << "s" << "study",
        "Path to study.yaml configuration file.",
        "file"
    );
    parser.addOption(studyOption);

    // Data root directory
    QCommandLineOption dataRootOption(
        QStringList() << "d" << "data-root",
        "Root directory containing scanner folders.",
        "directory"
    );
    parser.addOption(dataRootOption);

    // Output directory
    QCommandLineOption outputOption(
        QStringList() << "o" << "output",
        "Output directory for CSV files.",
        "directory",
        "./results"
    );
    parser.addOption(outputOption);

    // Parallel workers
    QCommandLineOption parallelOption(
        QStringList() << "p" << "parallel",
        "Number of parallel workers (default: CPU cores - 1).",
        "count"
    );
    parser.addOption(parallelOption);

    // Verbose output (no short option to avoid conflict with --version)
    QCommandLineOption verboseOption(
        "verbose",
        "Print detailed progress information."
    );
    parser.addOption(verboseOption);

    // ROI template file
    QCommandLineOption roiTemplateOption(
        QStringList() << "r" << "roi-template",
        "Path to ROI template JSON file with tooth segmentation settings.",
        "file"
    );
    parser.addOption(roiTemplateOption);

    parser.process(app);

    // Determine mode
    bool batchMode = parser.isSet(batchOption);

    if (batchMode) {
        // === CLI MODE ===
        std::cout << "DentScanComparePro v1.0 - Headless Batch Mode\n";
        std::cout << "=============================================\n";

        // Validate required options
        if (!parser.isSet(studyOption)) {
            std::cerr << "Error: --study option required in batch mode.\n";
            return 1;
        }
        if (!parser.isSet(dataRootOption)) {
            std::cerr << "Error: --data-root option required in batch mode.\n";
            return 1;
        }

        QString studyPath = parser.value(studyOption);
        QString dataRoot = parser.value(dataRootOption);
        QString outputDir = parser.value(outputOption);
        QString roiTemplatePath = parser.value(roiTemplateOption);
        bool verbose = parser.isSet(verboseOption);

        std::cout << "Study file:  " << studyPath.toStdString() << "\n";
        std::cout << "Data root:   " << dataRoot.toStdString() << "\n";
        std::cout << "Output dir:  " << outputDir.toStdString() << "\n";
        if (!roiTemplatePath.isEmpty()) {
            std::cout << "ROI template: " << roiTemplatePath.toStdString() << "\n";
        }
        std::cout << "\n" << std::flush;

        try {
            std::cout << "Loading configuration..." << std::flush;
            DentScanBatch::StudyConfig config = DentScanBatch::StudyConfig::loadFromFile(studyPath);
            std::cout << " done.\n";
            std::cout << "Study: " << config.name.toStdString() << "\n\n" << std::flush;

            // Load ROI template if provided
            std::optional<DentScanBatch::ROITemplate> roiTemplate;
            if (!roiTemplatePath.isEmpty()) {
                std::cout << "Loading ROI template..." << std::flush;
                roiTemplate = DentScanBatch::ROITemplate::loadFromFile(roiTemplatePath);
                std::cout << " done.\n";
                if (roiTemplate->useToothMask && !roiTemplate->toothSeeds.empty()) {
                    std::cout << "  Tooth segmentation enabled with "
                              << roiTemplate->toothSeeds.size() << " seed points.\n";
                }
                std::cout << std::flush;
            }

            DentScanBatch::BatchRunner runner;
            runner.setVerbose(verbose);

            bool success = runner.run(config, dataRoot, outputDir, roiTemplate);
            return success ? 0 : 1;
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        }

    } else {
        // === GUI MODE ===
        MainWindow window;
        window.show();
        return app.exec();
    }
}
