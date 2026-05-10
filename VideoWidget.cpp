#include "VideoWidget.h"
#include "Label.h"
#include "RangeSlider.h"
#include "ComfyBgRemover.h"
#include "VideoExporter.h"
#include "FrameExtractor.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QDebug>
#include <QDir>
#include <QMenuBar>
#include <QMenu>
#include <QActionGroup>
#include <QPainter>
#include <QSettings>
#include <QFileInfo>
#include <QKeyEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <cmath>

// ─── Constructor ────────────────────────────────────────────────────────────

VideoWidget::VideoWidget(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("VideoConverter");
    QCoreApplication::setOrganizationName("michaelSW");
    QCoreApplication::setOrganizationDomain("uyuni.de");
    QCoreApplication::setApplicationName("VideoConverter");

    // Menu
    QMenuBar *bar = new QMenuBar(this);
    setMenuBar(bar);
    QMenu *menu = new QMenu("Auflösung", bar);
    bar->addMenu(menu);
    QActionGroup *resGroup = new QActionGroup(this);
    resGroup->setExclusive(true);
    QAction *act1024 = menu->addAction("1024 × 1024");
    act1024->setCheckable(true);
    act1024->setChecked(true);
    resGroup->addAction(act1024);
    connect(act1024, &QAction::triggered, this, [this]{ m_resolution = 1024; });
    QAction *act2048 = menu->addAction("2048 × 2048");
    act2048->setCheckable(true);
    resGroup->addAction(act2048);
    connect(act2048, &QAction::triggered, this, [this]{ m_resolution = 2048; });

    QMenu *menuOpen = new QMenu("Öffnen", bar);
    bar->addMenu(menuOpen);
    QAction *actOpen = menuOpen->addAction("Datei öffnen …");
    actOpen->setShortcut(QKeySequence::Open);
    menuOpen->addSeparator();
    m_recentMenu = menuOpen->addMenu("Zuletzt geöffnet");
    connect(actOpen, &QAction::triggered, this, &VideoWidget::openFile);
    rebuildRecentMenu();

    QMenu *menuSave = new QMenu("Speichern", bar);
    bar->addMenu(menuSave);
    QAction *actSaveGrid    = menuSave->addAction("Sprite-Sheet …");
    QAction *actExportVideo = menuSave->addAction("Video exportieren … (MP4 / GIF / PNG-Sequenz)");
    menuSave->addSeparator();
    QMenu *menuOpenWith = menuSave->addMenu("Öffnen mit …");
    QAction *actOpenExplorer  = menuOpenWith->addAction("Explorer");
    QAction *actOpenXnView    = menuOpenWith->addAction("XnView");
    QAction *actOpenFastStone = menuOpenWith->addAction("FastStone");
    connect(actSaveGrid,       &QAction::triggered, this, &VideoWidget::saveSpriteSheet);
    connect(actExportVideo,    &QAction::triggered, this, &VideoWidget::exportVideo);
    connect(actOpenExplorer,   &QAction::triggered, this, &VideoWidget::openWithExplorer);
    connect(actOpenXnView,     &QAction::triggered, this, &VideoWidget::openWithXnView);
    connect(actOpenFastStone,  &QAction::triggered, this, &VideoWidget::openWithFastStone);

    QMenu *menuEdit = new QMenu("Bearbeiten", bar);
    bar->addMenu(menuEdit);
    QAction *actApplyCrop = menuEdit->addAction("Zuschnitt anwenden  (Pixel-Crop + Zeitbereich + Step)");
    connect(actApplyCrop, &QAction::triggered, this, &VideoWidget::applyCrop);
    m_actUndo = menuEdit->addAction("Rückgängig  (letzter Zuschnitt)");
    m_actUndo->setEnabled(false);
    connect(m_actUndo, &QAction::triggered, this, &VideoWidget::undoCrop);
    menuEdit->addSeparator();
    QAction *actPause = menuEdit->addAction("Pause / Weiter  [Space]");
    connect(actPause, &QAction::triggered, this, &VideoWidget::togglePause);

    QMenu *menuFx = new QMenu("Effekte", bar);
    bar->addMenu(menuFx);

    // Model selection submenu
    QMenu *menuModel = menuFx->addMenu("BiRefNet Modell");
    m_modelGroup = new QActionGroup(this);
    m_modelGroup->setExclusive(true);
    const QStringList models =
    {
        "ZhengPeng7/BiRefNet",
        "ZhengPeng7/BiRefNet_HR",
        "ZhengPeng7/BiRefNet-portrait"
    };
    for (const QString &m : models)
    {
        QAction *a = menuModel->addAction(m);
        a->setCheckable(true);
        a->setChecked(m == "ZhengPeng7/BiRefNet");
        m_modelGroup->addAction(a);
    }

    menuFx->addSeparator();
    m_actBgRemove = menuFx->addAction("Hintergrund entfernen (ComfyUI)");
    m_actBgRemove->setEnabled(false);
    connect(m_actBgRemove, &QAction::triggered, this, [this]
    {
        if (m_bgRemover)
        {
            m_bgRemover->cancel();
            m_bgRemover->deleteLater();
            m_bgRemover = nullptr;
            m_actBgRemove->setText("Hintergrund entfernen (ComfyUI)");
            setWindowTitle("VideoConverter");
        }
        else
        {
            startBgRemoval();
        }
    });

    // ── Layout ──
    resize(1000, 680);
    QWidget *central = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(2);

    m_stack = new QStackedWidget(central);

    // Page 0: live video/GIF
    m_label = new Label("Zieh ein Video hierher", m_stack);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_stack->addWidget(m_label);  // index 0

    // Page 1: sprite sheet + animated preview side by side
    QWidget *gridPage = new QWidget(m_stack);
    QHBoxLayout *gridLayout = new QHBoxLayout(gridPage);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(4);

    m_gridLabel = new QLabel(gridPage);
    m_gridLabel->setAlignment(Qt::AlignCenter);
    m_gridLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_gridLabel->setStyleSheet("background-color: #111;");
    m_gridLabel->installEventFilter(this);

    m_previewLabel = new QLabel(gridPage);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    m_previewLabel->setStyleSheet("background-color: #1a1a2e; border-left: 2px solid #444;");
    m_previewLabel->setMinimumWidth(180);
    m_previewLabel->setMaximumWidth(360);
    m_previewLabel->installEventFilter(this);

    gridLayout->addWidget(m_gridLabel, 3);
    gridLayout->addWidget(m_previewLabel, 1);
    m_stack->addWidget(gridPage);  // index 1

    mainLayout->addWidget(m_stack, 1);

    // Bottom controls
    QHBoxLayout *ctrlLayout = new QHBoxLayout();
    ctrlLayout->setContentsMargins(4, 0, 4, 4);
    m_labelLower  = new QLabel("–",   central);
    m_rangeSlider = new RangeSlider(central, Qt::Horizontal);
    m_labelUpper  = new QLabel("–",   central);
    m_labelSort   = new QLabel("–",   central);
    m_sortSlider  = new QSlider(Qt::Horizontal, central);
    m_sortSlider->setMaximumWidth(80);
    m_sortSlider->setMinimum(1);
    m_sortSlider->setValue(1);
    m_rangeSlider->blockSignals(true);
    m_sortSlider->blockSignals(true);
    ctrlLayout->addWidget(m_labelLower);
    ctrlLayout->addWidget(m_rangeSlider, 1);
    ctrlLayout->addWidget(m_labelUpper);
    ctrlLayout->addWidget(m_labelSort);
    ctrlLayout->addWidget(m_sortSlider);
    mainLayout->addLayout(ctrlLayout);

    setCentralWidget(central);

    // Connections
    connect(m_label, &Label::rightClicked, this, &VideoWidget::toggleView);
    connect(&m_previewTimer, &QTimer::timeout, this, &VideoWidget::previewTick);
    connect(&m_playTimer,    &QTimer::timeout, this, &VideoWidget::playTick);
    connect(m_rangeSlider, &RangeSlider::lowerValueChanged, this, &VideoWidget::lowerValueChanged);
    connect(m_rangeSlider, &RangeSlider::upperValueChanged, this, &VideoWidget::upperValueChanged);
    connect(m_sortSlider,  &QSlider::valueChanged,          this, &VideoWidget::sortValueChanged);

    setAcceptDrops(true);

    QMetaObject::invokeMethod(this, [this]
    {
        const QStringList hist = QSettings().value("history/files").toStringList();
        if (!hist.isEmpty()) doDropEvent(hist.first());
    }, Qt::QueuedConnection);
}

// ─── View toggle ────────────────────────────────────────────────────────────

void VideoWidget::toggleView()
{
    if (m_bigMap.isEmpty()) return;
    m_showingGrid = !m_showingGrid;
    m_stack->setCurrentIndex(m_showingGrid ? 1 : 0);
    if (m_showingGrid)
    {
        m_playTimer.stop();
        paintGrid();
    }
    else
    {
        m_previewTimer.stop();
        if (!m_fillingMap) startPlayback();
        // updateTitle() wird von startPlayback() aufgerufen
    }
}

bool VideoWidget::eventFilter(QObject *obj, QEvent *event)
{
    if ((obj == m_gridLabel || obj == m_previewLabel)
        && event->type() == QEvent::MouseButtonRelease)
    {
        if (static_cast<QMouseEvent*>(event)->button() == Qt::RightButton)
        {
            toggleView();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// ─── Grid layout optimisation ────────────────────────────────────────────────

VideoWidget::GridDims VideoWidget::findOptimalGrid(int N, double cropAspect) const
{
    if (N <= 0) return {1, 1};
    if (cropAspect <= 0.0) cropAspect = 1.0;

    int    bestCols = 1, bestRows = N;
    double bestScore = 1e9;

    for (int cols = 1; cols <= N; ++cols)
    {
        const int baseRows = (N + cols - 1) / cols;
        for (int extra = 0; extra <= 1; ++extra) {          // try +0 and +1 extra row
            const int rows  = baseRows + extra;
            const int waste = cols * rows - N;

            // cell aspect ratio for a square texture
            const double cellAspect = static_cast<double>(rows) / cols;

            // symmetric log-ratio: 0 = perfect, grows for over- and under-stretch
            const double distortion = std::abs(std::log(cellAspect / cropAspect));

            // waste penalty is mild — user accepts +1 row/col
            const double score = distortion + waste * 0.15;

            if (score < bestScore)
            {
                bestScore = score;
                bestCols  = cols;
                bestRows  = rows;
            }
        }
    }
    return {bestCols, bestRows};
}

double VideoWidget::getCropAspect() const
{
    return m_label->cropAspectRatio();
}

// ─── Playback through m_bigMap selection ─────────────────────────────────────

void VideoWidget::togglePause()
{
    if (m_fillingMap || m_bigMap.isEmpty()) return;
    m_paused = !m_paused;
    if (m_paused)
    {
        m_playTimer.stop();
    }
    else
    {
        // Play startet immer vom unteren Griff
        m_playIndex = m_rangeSlider->lowerValue();
        m_playTimer.start(qMax(16, m_delay * m_sortSlider->value()));
    }
    updateTitle();
}

void VideoWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space)
        togglePause();
    else
        QMainWindow::keyPressEvent(event);
}

void VideoWidget::startPlayback()
{
    m_playIndex = m_rangeSlider->lowerValue();
    if (!m_paused)
        m_playTimer.start(qMax(16, m_delay * m_sortSlider->value()));
    updateTitle();
}

void VideoWidget::playTick()
{
    const int first = m_rangeSlider->lowerValue();
    const int last  = m_rangeSlider->upperValue();
    const int step  = m_sortSlider->value();

    if (!m_bigMap.contains(m_playIndex))
        m_playIndex = first;

    const QRect cropRect = m_label->cropRectInImageCoords();
    QPixmap px = m_bigMap[m_playIndex];
    if (!cropRect.isEmpty() && cropRect != px.rect())
        px = px.copy(cropRect);

    m_label->setImage(px, m_playIndex, m_bigMap.size(), m_delay);
    m_label->update();

    m_playIndex += step;
    if (m_playIndex > last)
        m_playIndex = first;
}

// ─── Grid composing ─────────────────────────────────────────────────────────

QPixmap VideoWidget::composeGrid(int first, int frameCount, int step)
{
    const int N = qMax(1, frameCount / step);
    const double cropAspect = getCropAspect();
    const auto [cols, rows] = findOptimalGrid(N, cropAspect);
    m_gridCols = cols;
    m_gridRows = rows;

    const int cellW = m_resolution / cols;
    const int cellH = m_resolution / rows;

    QPixmap result(m_resolution, m_resolution);
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    m_previewList.clear();
    const QRect cropRect = m_label->cropRectInImageCoords();

    for (int i = 0; i < N; ++i)
    {
        const int key = first + i * step;
        if (!m_bigMap.contains(key)) continue;

        // Apply crop if one is set
        QPixmap src = m_bigMap[key];
        if (!cropRect.isEmpty() && cropRect != src.rect())
            src = src.copy(cropRect);

        m_previewList << src;

        const int row = i / cols;
        const int col = i % cols;
        const QRect cell(col * cellW, row * cellH, cellW, cellH);

        p.drawPixmap(cell, src.scaled(cell.size(),
                                      Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation));
    }

    // Window title: grid info + stretch factor
    const double cellAspect = static_cast<double>(rows) / cols;
    const double stretch    = (cropAspect > 0.0) ? cellAspect / cropAspect : 1.0;
    const int    waste      = cols * rows - N;
    setWindowTitle(
        QString("Frame Grabber  —  %1×%2  |  %3 frames  |  Stretch %4×  |  Verschnitt %5")
            .arg(cols).arg(rows).arg(N)
            .arg(QString::number(stretch, 'f', 2))
            .arg(waste));

    return result;
}

void VideoWidget::paintGrid()
{
    if (m_bigMap.isEmpty()) return;
    const int first = m_rangeSlider->lowerValue();
    const int last  = m_rangeSlider->upperValue();
    const int step  = m_sortSlider->value();

    const QPixmap grid = composeGrid(first, last - first + 1, step);
    m_gridLabel->setPixmap(grid.scaled(
        m_gridLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // Start preview animation
    m_previewIndex = 0;
    if (!m_previewList.isEmpty())
    {
        m_previewLabel->setPixmap(m_previewList[0].scaled(
            m_previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        const int interval = qMax(16, m_delay * step);
        m_previewTimer.start(interval);
    }
}

// ─── Slider slots ───────────────────────────────────────────────────────────

void VideoWidget::showFrame(int index)
{
    if (!m_bigMap.contains(index)) return;
    QPixmap px = m_bigMap[index];
    const QRect cr = m_label->cropRectInImageCoords();
    if (!cr.isEmpty() && cr != px.rect()) px = px.copy(cr);
    m_label->setImage(px, index, m_bigMap.size(), m_delay);
    m_label->update();
}

void VideoWidget::lowerValueChanged(int value)
{
    m_labelLower->setText(QString::number(value));
    if (value >= m_rangeSlider->upperValue())
    {
        m_rangeSlider->setLowerValue(m_rangeSlider->upperValue() - 1);
        return;
    }
    m_lastHandle = LowerHandle;
    if (m_showingGrid)
    {
        paintGrid();
    }
    else if (!m_fillingMap)
    {
        // Slider bewegt → automatisch pausieren und Frame zeigen
        m_paused = true;
        m_playTimer.stop();
        m_playIndex = value;
        showFrame(value);
    }
    updateTitle();
}

void VideoWidget::upperValueChanged(int value)
{
    m_labelUpper->setText(QString::number(value));
    if (value <= m_rangeSlider->lowerValue())
    {
        m_rangeSlider->setUpperValue(m_rangeSlider->lowerValue() + 1);
        return;
    }
    m_lastHandle = UpperHandle;
    if (m_showingGrid)
    {
        paintGrid();
    }
    else if (!m_fillingMap)
    {
        // Slider bewegt → automatisch pausieren und Frame zeigen
        m_paused = true;
        m_playTimer.stop();
        m_playIndex = value;
        showFrame(value);
    }
    updateTitle();
}

void VideoWidget::sortValueChanged(int value)
{
    const int maxSort = qMax(1, (m_rangeSlider->upperValue()
                                 - m_rangeSlider->lowerValue() + 1) / 2);
    m_labelSort->setText(QString("%1/%2").arg(value).arg(maxSort));
    if (m_showingGrid)
        paintGrid();
    else if (!m_fillingMap && !m_paused)
        m_playTimer.start(qMax(16, m_delay * value));
    updateTitle();
}

// ─── Preview animation ───────────────────────────────────────────────────────

void VideoWidget::previewTick()
{
    if (m_previewList.isEmpty()) return;
    m_previewIndex = (m_previewIndex + 1) % m_previewList.size();

    const int cellW = m_resolution / m_gridCols;
    const int cellH = m_resolution / m_gridRows;

    // Scale to actual sprite-sheet cell size (= real quality in SL texture)
    const QPixmap cellSized = m_previewList[m_previewIndex].scaled(
        cellW, cellH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // Upscale without smoothing → pixelation visible = honest quality preview
    m_previewLabel->setPixmap(cellSized.scaled(
        m_previewLabel->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

// ─── Drag & Drop ────────────────────────────────────────────────────────────

void VideoWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void VideoWidget::dropEvent(QDropEvent *event)
{
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty())
        doDropEvent(urls.first().toLocalFile());
}

void VideoWidget::doDropEvent(const QString &path)
{
    if (path.isEmpty()) return;

    m_previewTimer.stop();
    m_playTimer.stop();
    if (m_extractor) { m_extractor->cancel(); m_extractor->deleteLater(); m_extractor = nullptr; }
    if (m_bgRemover) { m_bgRemover->cancel(); m_bgRemover->deleteLater(); m_bgRemover = nullptr; }
    m_actBgRemove->setText("Hintergrund entfernen (ComfyUI)");
    m_actBgRemove->setEnabled(false);

    m_label->resetSelAdjList();
    m_bigMap.clear();
    m_previewList.clear();
    m_undoStack.clear();
    m_actUndo->setEnabled(false);
    m_delay       = 0;
    m_fillingMap  = true;
    m_paused      = false;
    m_lastHandle  = NoHandle;
    m_showingGrid = false;
    m_stack->setCurrentIndex(0);
    m_rangeSlider->setEnabled(false);
    m_sortSlider->setEnabled(false);
    m_rangeSlider->blockSignals(true);
    m_sortSlider->blockSignals(true);
    m_labelLower->setText("–");
    m_labelUpper->setText("–");
    m_labelSort->setText("–");
    setWindowTitle("VideoConverter — extrahiere Frames …");

    static const QStringList imageExts = {"png","jpg","jpeg","bmp","tif","tiff","webp"};
    const QString ext = QFileInfo(path).suffix().toLower();

    if (imageExts.contains(ext))
    {
        // Einzelbild → sofort laden
        const QPixmap px(path);
        if (px.isNull()) return;
        onFramesExtracted({{0, px}}, 40);
    }
    else
    {
        // Video oder GIF → FrameExtractor
        m_extractor = new FrameExtractor("ffmpeg", this);
        connect(m_extractor, &FrameExtractor::progress, this, [this](int done, int total)
        {
            setWindowTitle(QString("VideoConverter — extrahiere Frames … (%1/%2)")
                           .arg(done).arg(total));
        });
        connect(m_extractor, &FrameExtractor::finished,
                this, &VideoWidget::onFramesExtracted);
        connect(m_extractor, &FrameExtractor::error, this, [this](const QString &msg)
        {
            setWindowTitle("VideoConverter");
            m_fillingMap = false;
            QMessageBox::warning(this, "Extraktion fehlgeschlagen", msg);
        });
        m_extractor->extract(path);
    }

    addToHistory(path);
}

void VideoWidget::onFramesExtracted(QMap<int, QPixmap> frames, int delayMs)
{
    if (m_extractor) { m_extractor->deleteLater(); m_extractor = nullptr; }

    if (frames.isEmpty())
    {
        setWindowTitle("VideoConverter");
        m_fillingMap = false;
        return;
    }

    m_bigMap  = frames;
    m_delay   = delayMs;
    const int count = m_bigMap.size();

    m_rangeSlider->setRange(0, count - 1);
    m_rangeSlider->setLowerValue(0);
    m_rangeSlider->setUpperValue(count - 1);
    const int maxSort = qMax(1, count / 2);
    m_sortSlider->setRange(1, maxSort);
    m_sortSlider->setValue(1);
    m_labelLower->setText("0");
    m_labelUpper->setText(QString::number(count - 1));
    m_labelSort->setText(QString("1/%1").arg(maxSort));
    m_rangeSlider->blockSignals(false);
    m_sortSlider->blockSignals(false);
    m_rangeSlider->setEnabled(true);
    m_sortSlider->setEnabled(true);
    m_fillingMap = false;
    m_actBgRemove->setEnabled(true);

    // Erstes Bild anzeigen
    showFrame(0);
    startPlayback();
}

// ─── Undo ────────────────────────────────────────────────────────────────────

void VideoWidget::pushUndo()
{
    UndoState s;
    s.bigMap = m_bigMap;
    s.delay  = m_delay;
    s.lower  = m_rangeSlider->lowerValue();
    s.upper  = m_rangeSlider->upperValue();
    s.step   = m_sortSlider->value();
    m_undoStack.push(s);
    m_actUndo->setEnabled(true);
}

void VideoWidget::updateTitle()
{
    if (m_fillingMap || m_bigMap.isEmpty()) return;
    const int first = m_rangeSlider->lowerValue();
    const int last  = m_rangeSlider->upperValue();
    const int step  = m_sortSlider->value();
    const double fps = step > 0 && m_delay > 0 ? 1000.0 / (m_delay * step) : 0.0;
    const QString status = m_paused ? "  ⏸ PAUSE" : "";
    setWindowTitle(QString("VideoConverter  —  [%1 … %2]  step %3  |  %4 fps%5")
                   .arg(first).arg(last).arg(step)
                   .arg(fps, 0, 'f', 1).arg(status));
}

// ─── Background removal ───────────────────────────────────────────────────────

void VideoWidget::startBgRemoval()
{
    if (m_bigMap.isEmpty()) return;

    // Undo-State sichern — so kann man zurück und ein anderes Modell probieren
    pushUndo();

    // Only process frames in current slider range with step
    const int first = m_rangeSlider->lowerValue();
    const int last  = m_rangeSlider->upperValue();
    const int step  = m_sortSlider->value();
    QMap<int, QPixmap> toProcess;
    for (int i = first; i <= last; i += step)
        if (m_bigMap.contains(i))
            toProcess[i] = m_bigMap[i];
    if (toProcess.isEmpty()) return;

    m_actBgRemove->setText("Abbrechen");
    setWindowTitle(QString("Hintergrund wird entfernt … (0/%1)").arg(toProcess.size()));

    m_bgRemover = new ComfyBgRemover("http://127.0.0.1:8188", this);
    connect(m_bgRemover, &ComfyBgRemover::frameReady, this, &VideoWidget::onBgFrameReady);
    connect(m_bgRemover, &ComfyBgRemover::progress,   this, &VideoWidget::onBgProgress);
    connect(m_bgRemover, &ComfyBgRemover::finished,   this, &VideoWidget::onBgFinished);
    connect(m_bgRemover, &ComfyBgRemover::error, this, [this](const QString &msg)
    {
        setWindowTitle("Fehler: " + msg);
        m_actBgRemove->setText("Hintergrund entfernen (ComfyUI)");
        m_bgRemover->deleteLater();
        m_bgRemover = nullptr;
    });

    const QAction *checked = m_modelGroup->checkedAction();
    const QString model = checked ? checked->text() : "ZhengPeng7/BiRefNet";
    m_bgRemover->process(toProcess, model);
}

void VideoWidget::onBgFrameReady(int index, QPixmap result)
{
    m_bigMap[index] = result;
    // Show current frame live if it's visible
    if (!m_showingGrid && m_label->currentIndex() == index)
    {
        m_label->setImage(result, index, m_bigMap.size(), m_delay);
        m_label->update();
    }
}

void VideoWidget::onBgProgress(int done, int total)
{
    setWindowTitle(QString("Hintergrund wird entfernt … (%1/%2)").arg(done).arg(total));
}

void VideoWidget::onBgFinished()
{
    setWindowTitle("VideoConverter");
    m_actBgRemove->setText("Hintergrund entfernen (ComfyUI)");
    m_bgRemover->deleteLater();
    m_bgRemover = nullptr;
    if (m_showingGrid)
        paintGrid();
    else
    {
        const int cur = m_rangeSlider->lowerValue();
        if (m_bigMap.contains(cur))
        {
            m_label->setImage(m_bigMap[cur], cur, m_bigMap.size(), m_delay);
            m_label->update();
        }
    }
}

// ─── Speichern ────────────────────────────────────────────────────────────────

void VideoWidget::applyCrop()
{
    if (m_bigMap.isEmpty()) return;

    pushUndo();

    const QRect cropRect = m_label->cropRectInImageCoords();
    const int first = m_rangeSlider->lowerValue();
    const int last  = m_rangeSlider->upperValue();
    const int step  = m_sortSlider->value();

    // Remember the current frame size — crop result will be scaled back up to this
    const QSize targetSize = m_bigMap.first().size();

    QMap<int, QPixmap> newMap;
    int newIndex = 0;
    for (int i = first; i <= last; i += step)
    {
        if (!m_bigMap.contains(i)) continue;
        QPixmap px = m_bigMap[i];
        if (!cropRect.isEmpty() && cropRect != px.rect())
        {
            px = px.copy(cropRect);
            if (px.width() < targetSize.width() || px.height() < targetSize.height())
                px = px.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        newMap[newIndex++] = px;
    }
    if (newMap.isEmpty()) return;

    m_bigMap = newMap;
    m_delay  = qMax(16, m_delay * step);

    const int count = m_bigMap.size();
    m_rangeSlider->blockSignals(true);
    m_sortSlider->blockSignals(true);
    m_rangeSlider->setRange(0, count - 1);
    m_rangeSlider->setLowerValue(0);
    m_rangeSlider->setUpperValue(count - 1);
    const int maxSort = qMax(1, count / 2);
    m_sortSlider->setRange(1, maxSort);
    m_sortSlider->setValue(1);
    m_labelLower->setText("0");
    m_labelUpper->setText(QString::number(count - 1));
    m_labelSort->setText(QString("1/%1").arg(maxSort));
    m_rangeSlider->blockSignals(false);
    m_sortSlider->blockSignals(false);

    m_label->resetSelAdjList();
    m_label->setImage(m_bigMap[0], 0, count, m_delay);
    m_label->update();

    if (m_showingGrid) paintGrid();
}

void VideoWidget::exportVideo()
{
    if (m_bigMap.isEmpty()) return;

    // Format-Auswahl über Datei-Filter
    const QString filter =
        "MP4 Video H.264 — kein Alpha (*.mp4);;"
        "Animiertes GIF — binäres Alpha (*.gif);;"
        "PNG-Sequenz — volles Alpha, für Weiterverarbeitung (*.png)";

    QSettings s;
    const QString path = QFileDialog::getSaveFileName(
        this, "Video exportieren",
        s.value("save/dir").toString(),
        filter);
    if (path.isEmpty()) return;

    VideoExporter::Options opts;
    opts.fps        = m_delay > 0 ? 1000.0 / m_delay : 25.0;
    opts.outputPath = path;

    if (path.endsWith(".mp4", Qt::CaseInsensitive))      opts.format = VideoExporter::MP4;
    else if (path.endsWith(".gif", Qt::CaseInsensitive)) opts.format = VideoExporter::GIF;
    else                                                  opts.format = VideoExporter::PNG_Sequence;

    // Collect frames in slider range with step + crop applied
    const int first = m_rangeSlider->lowerValue();
    const int last  = m_rangeSlider->upperValue();
    const int step  = m_sortSlider->value();
    const QRect cropRect = m_label->cropRectInImageCoords();

    QMap<int, QPixmap> toExport;
    int idx = 0;
    for (int i = first; i <= last; i += step)
    {
        if (!m_bigMap.contains(i)) continue;
        QPixmap px = m_bigMap[i];
        if (!cropRect.isEmpty() && cropRect != px.rect())
            px = px.copy(cropRect);
        toExport[idx++] = px;
    }
    if (toExport.isEmpty()) return;

    auto *exporter = new VideoExporter(this);
    connect(exporter, &VideoExporter::progress, this, [this](int done, int total)
    {
        setWindowTitle(QString("Exportiere … (%1/%2)").arg(done).arg(total));
    });
    connect(exporter, &VideoExporter::finished, this, [this, exporter](const QString &out)
    {
        setWindowTitle("VideoConverter");
        QSettings().setValue("save/dir", QFileInfo(out).absolutePath());
        exporter->deleteLater();
        QMessageBox::information(this, "Export fertig", "Gespeichert:\n" + out);
    });
    connect(exporter, &VideoExporter::error, this, [this, exporter](const QString &msg)
    {
        setWindowTitle("VideoConverter");
        exporter->deleteLater();
        QMessageBox::warning(this, "Export-Fehler", msg);
    });

    exporter->exportFrames(toExport, opts);
}

void VideoWidget::undoCrop()
{
    if (m_undoStack.isEmpty()) return;

    const UndoState s = m_undoStack.pop();
    m_bigMap = s.bigMap;
    m_delay  = s.delay;
    m_actUndo->setEnabled(!m_undoStack.isEmpty());

    m_rangeSlider->blockSignals(true);
    m_sortSlider->blockSignals(true);
    m_rangeSlider->setRange(0, m_bigMap.size() - 1);
    m_rangeSlider->setLowerValue(s.lower);
    m_rangeSlider->setUpperValue(s.upper);
    m_sortSlider->setRange(1, qMax(1, m_bigMap.size() / 2));
    m_sortSlider->setValue(s.step);
    m_labelLower->setText(QString::number(s.lower));
    m_labelUpper->setText(QString::number(s.upper));
    m_labelSort->setText(QString("%1/%2").arg(s.step)
                         .arg(qMax(1, m_bigMap.size() / 2)));
    m_rangeSlider->blockSignals(false);
    m_sortSlider->blockSignals(false);

    if (!m_bigMap.isEmpty())
    {
        m_label->setImage(m_bigMap[s.lower], s.lower, m_bigMap.size(), m_delay);
        m_label->update();
    }
    if (m_showingGrid) paintGrid();
    else startPlayback();
}

void VideoWidget::saveSpriteSheet()
{
    if (m_bigMap.isEmpty()) return;

    const int first = m_rangeSlider->lowerValue();
    const int last  = m_rangeSlider->upperValue();
    const int step  = m_sortSlider->value();
    const int N     = qMax(1, (last - first + 1) / step);
    const auto [cols, rows] = findOptimalGrid(N, getCropAspect());
    const double fps = m_delay > 0 ? 1000.0 / (m_delay * step) : 25.0;

    QSettings s;
    const QString defaultName = QString("%1/%2x%3_%4frames_%5fps.png")
        .arg(s.value("save/dir").toString())
        .arg(cols).arg(rows).arg(N)
        .arg(fps, 0, 'f', 1);

    const QString path = QFileDialog::getSaveFileName(
        this, "Sprite-Sheet speichern", defaultName, "PNG (*.png);;All files (*)");
    if (path.isEmpty()) return;

    const QPixmap grid = composeGrid(first, last - first + 1, step);
    if (!grid.save(path, "PNG")) return;
    s.setValue("save/dir", QFileInfo(path).absolutePath());

    // LSL-Script daneben ablegen
    const QString lslPath = QFileInfo(path).absolutePath() + "/"
                          + QFileInfo(path).baseName() + ".lsl";
    QFile lslFile(lslPath);
    if (lslFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream ts(&lslFile);
        ts << "// Auto-generated by VideoConverter\n";
        ts << "// Sprite-Sheet: " << cols << "x" << rows
           << ", " << N << " frames @ " << QString::number(fps, 'f', 1) << " fps\n";
        ts << "llSetTextureAnim(ANIM_ON | LOOP, ALL_SIDES, "
           << cols << ", " << rows << ", 0, " << N << ", "
           << QString::number(fps, 'f', 2) << ");\n";
    }
}

// ─── File open / history ─────────────────────────────────────────────────────

void VideoWidget::openFile()
{
    const QStringList hist = QSettings().value("history/files").toStringList();
    const QString startDir = hist.isEmpty()
        ? QString() : QFileInfo(hist.first()).absolutePath();

    const QString path = QFileDialog::getOpenFileName(
        this, "Datei öffnen", startDir,
        "Alle unterstützten Dateien (*.mp4 *.mov *.avi *.mkv *.gif "
            "*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp);;"
        "Videos (*.mp4 *.mov *.avi *.mkv *.gif);;"
        "Bilder (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp);;"
        "Alle Dateien (*)");
    if (!path.isEmpty()) doDropEvent(path);
}

void VideoWidget::addToHistory(const QString &path)
{
    QSettings s;
    s.setValue("video", path);
    QStringList hist = s.value("history/files").toStringList();
    hist.removeAll(path);
    hist.prepend(path);
    if (hist.size() > 20) hist.resize(20);
    s.setValue("history/files", hist);
    rebuildRecentMenu();
}

void VideoWidget::rebuildRecentMenu()
{
    m_recentMenu->clear();
    const QStringList hist = QSettings().value("history/files").toStringList();
    if (hist.isEmpty())
    {
        QAction *empty = m_recentMenu->addAction("(leer)");
        empty->setEnabled(false);
        return;
    }
    for (const QString &f : hist)
    {
        QAction *a = m_recentMenu->addAction(
            QString("%1  —  %2").arg(QFileInfo(f).fileName(), QFileInfo(f).absolutePath()));
        connect(a, &QAction::triggered, this, [this, f]{ doDropEvent(f); });
    }
}

// ─── Open with ───────────────────────────────────────────────────────────────

void VideoWidget::openWithExplorer()
{
    const QString dir = QSettings().value("save/dir").toString();
    if (dir.isEmpty() || !QDir(dir).exists())
    {
        QMessageBox::information(this, "Kein Pfad", "Noch kein Speicherpfad bekannt.");
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void VideoWidget::openWithXnView()
{
    openWithViewer("viewer/xnview", "XnView-Programmdatei wählen");
}

void VideoWidget::openWithFastStone()
{
    openWithViewer("viewer/faststone", "FastStone-Programmdatei wählen");
}

void VideoWidget::openWithViewer(const QString &settingsKey, const QString &title)
{
    QSettings s;
    QString exe = s.value(settingsKey).toString();
    if (exe.isEmpty() || !QFileInfo::exists(exe))
    {
        exe = QFileDialog::getOpenFileName(
            this, title,
            "C:/Program Files",
            "Ausführbare Dateien (*.exe);;Alle Dateien (*)");
        if (exe.isEmpty()) return;
        s.setValue(settingsKey, exe);
    }
    const QString dir = s.value("save/dir").toString();
    if (dir.isEmpty() || !QDir(dir).exists())
    {
        QMessageBox::information(this, "Kein Pfad", "Noch kein Speicherpfad bekannt.");
        return;
    }
    QProcess::startDetached(exe, { dir });
}
