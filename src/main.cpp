// CompressorWindows — minimal Qt facade over CompressionLib.
// UI layer only: parsing/rendering/state. All compression lives in the engine.
#include <compression/app/CompressionService.hpp>
#include <compression/app/ArchiveService.hpp>
#include <compression/codec/CodecRegistry.hpp>
#include <compression/events/EventBus.hpp>

#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>

#include <QMetaObject>
#include <memory>
#include <stdexcept>

namespace {

using compression::CompressionService;
using compression::CompressionOptions;
using compression::CompressResult;
using compression::ExtractResult;

// Marshals engine events (any thread) to the GUI thread. Progress events are
// emitted by worker threads inside ParallelCodecDecorator.
class UiEventBridge final : public QObject,
                            public compression::events::IEventListener {
  Q_OBJECT
public:
  void onEvent(const compression::events::CompressionEvent &event) override {
    QMetaObject::invokeMethod(this, "onGuiThread",
                              Qt::QueuedConnection,
                              Q_ARG(int, static_cast<int>(event.type)),
                              Q_ARG(int, static_cast<int>(event.progressPct)));
  }

signals:
  void onGuiThread(int type, int progressPct);
};

} // namespace

class MainWindow : public QMainWindow {
public:
  explicit MainWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
    setWindowTitle("Compressor for Windows");
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    auto *inputGroup = new QGroupBox("File", central);
    auto *form = new QFormLayout(inputGroup);
    inputEdit_ = new QLineEdit(inputGroup);
    outputEdit_ = new QLineEdit(inputGroup);
    auto *browseIn = new QPushButton("Browse...", inputGroup);
    auto *browseOut = new QPushButton("Browse...", inputGroup);
    auto *inRow = new QHBoxLayout();
    inRow->addWidget(inputEdit_);
    inRow->addWidget(browseIn);
    auto *outRow = new QHBoxLayout();
    outRow->addWidget(outputEdit_);
    outRow->addWidget(browseOut);
    form->addRow("Input file", inRow);
    form->addRow("Output file", outRow);
    layout->addWidget(inputGroup);

    strategyCombo_ = new QComboBox(central);
    for (const auto &[id, name] :
         compression::codec::CodecRegistry::instance().all()) {
      strategyCombo_->addItem(QString::fromStdString(name),
                              static_cast<int>(id));
    }
    strategyCombo_->setCurrentText("optimized");
    layout->addWidget(strategyCombo_);

    progress_ = new QProgressBar(central);
    progress_->setRange(0, 100);
    layout->addWidget(progress_);

    auto *buttons = new QHBoxLayout();
    auto *compressBtn = new QPushButton("Compress", central);
    auto *decompressBtn = new QPushButton("Decompress", central);
    buttons->addWidget(compressBtn);
    buttons->addWidget(decompressBtn);
    layout->addLayout(buttons);
    setCentralWidget(central);

    connect(browseIn, &QPushButton::clicked, this, [this] {
      const QString f = QFileDialog::getOpenFileName(
          this, "Choose input file");
      if (!f.isEmpty())
        inputEdit_->setText(f);
    });
    connect(browseOut, &QPushButton::clicked, this, [this] {
      const QString f = QFileDialog::getSaveFileName(
          this, "Choose output file");
      if (!f.isEmpty())
        outputEdit_->setText(f);
    });
    connect(compressBtn, &QPushButton::clicked, this, [this] { run(true); });
    connect(decompressBtn, &QPushButton::clicked, this,
            [this] { run(false); });
    connect(&bridge_, &UiEventBridge::onGuiThread, this,
            [this](int /*type*/, int progressPct) {
              progress_->setValue(progressPct);
            });
  }

private:
  void run(bool compress) {
    if (inputEdit_->text().isEmpty() || outputEdit_->text().isEmpty()) {
      QMessageBox::warning(this, "Missing paths",
                           "Choose an input and an output file first.");
      return;
    }
    try {
      auto bus = std::make_shared<compression::events::EventBus>();
      bus->subscribe(std::make_shared<UiEventBridge>());
      CompressionService service(bus);
      if (compress) {
        CompressionOptions options;
        options.codec = static_cast<compression::format::AlgorithmID>(
            strategyCombo_->currentData().toInt());
        const CompressResult r = service.compressFile(
            inputEdit_->text().toStdWString(),
            outputEdit_->text().toStdWString(), options);
        statusBar()->showMessage(
            QString("Compressed %1 -> %2 bytes (ratio %3%, CRC 0x%4)")
                .arg(r.inBytes)
                .arg(r.outBytes)
                .arg(r.ratio * 100.0, 0, 'f', 2)
                .arg(r.crc, 8, 16, QLatin1Char('0')));
      } else {
        const ExtractResult r = service.decompressFile(
            inputEdit_->text().toStdWString(),
            outputEdit_->text().toStdWString());
        statusBar()->showMessage(
            QString("Decompressed %1 -> %2 bytes, CRC verified: %3")
                .arg(r.inBytes)
                .arg(r.outBytes)
                .arg(r.verified ? "yes" : "NO"));
        if (!r.verified) {
          QMessageBox::critical(this, "CRC mismatch",
                                "Decompressed data failed CRC verification.");
        }
      }
    } catch (const std::exception &e) {
      QMessageBox::critical(this, "Engine error",
                            QString::fromUtf8(e.what()));
    }
  }

  QLineEdit *inputEdit_ = nullptr;
  QLineEdit *outputEdit_ = nullptr;
  QComboBox *strategyCombo_ = nullptr;
  QProgressBar *progress_ = nullptr;
  UiEventBridge bridge_;
};

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  MainWindow w;
  w.resize(560, 220);
  w.show();
  return app.exec();
}
#include "main.moc"
