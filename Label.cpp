#include "Label.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QPainter>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QMenu>
#include <QWheelEvent>
#include <cmath>

void Label::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton) return;
    const bool ctrl = ev->modifiers() & Qt::ControlModifier;
    if (m_erasing)
    {
        if (ctrl)
        {
            // Erneutes Strg+Linksklick: nachfragen, wie es weitergeht
            showEraseMenu(ev->globalPosition().toPoint());
            return;
        }
        m_eraserPos    = clampToWidget(ev->position());
        m_strokeTarget = m_erasePerFrame ? imagePlus.index : kGlobalStroke;
        EraseMask &mask = m_strokeTarget == kGlobalStroke
                              ? m_globalMask : m_frameMasks[m_strokeTarget];
        mask << EraseStroke{ QPolygonF{ toImage(m_eraserPos) }, m_eraseRadius };
        update();
        return;
    }
    if (ctrl)
    {
        beginErase(ev->position());
        return;
    }
    // Zuschnitt nur noch mit Shift+Linksklick, ein einfacher Klick tut nichts
    if (!(ev->modifiers() & Qt::ShiftModifier)) return;
    origin = ev->pos();
    if (!rubberBand)
        rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
    // Stand fuer "Abbrechen" im Auswahldialog merken
    m_cropBeforeClick = m_imageCropRect;
    m_padBeforeClick  = m_padRatio;
    // Waehrend des Aufziehens das unveraenderte Bild zeigen, sonst wuerde die
    // Auswahl gegen ein bereits zugeschnittenes/erweitertes Bild gemessen.
    m_imageCropRect = QRect();
    m_padRatio      = QSize();
    emit cropChanged();
    rubberBand->setGeometry(QRect(origin, QSize()));
    rubberBand->show();
}

void Label::mouseMoveEvent(QMouseEvent *ev)
{
    if (m_erasing)
    {
        m_eraserPos = clampToWidget(ev->position());
        if (m_strokeTarget != kNoStroke)
        {
            EraseMask &mask = m_strokeTarget == kGlobalStroke
                                  ? m_globalMask : m_frameMasks[m_strokeTarget];
            if (!mask.isEmpty()) mask.last().points << toImage(m_eraserPos);
        }
        update();
        return;
    }
    if (rubberBand && rubberBand->isVisible())
        rubberBand->setGeometry(QRect(origin, ev->pos()).normalized());
}

void Label::keyPressEvent(QKeyEvent *ev)
{
    if (m_erasing && ev->key() == Qt::Key_Escape)
    {
        // Wie Strg+Linksklick: nachfragen, wie es weitergeht
        showEraseMenu(QCursor::pos());
        return;
    }
    QLabel::keyPressEvent(ev);
}

// Mausrad im Radiermodus: Radierer vergrößern bzw. verkleinern
void Label::wheelEvent(QWheelEvent *ev)
{
    if (!m_erasing)
    {
        QLabel::wheelEvent(ev);
        return;
    }
    const qreal steps = ev->angleDelta().y() / 120.0;
    const qreal maxR  = 0.5 * qMax(imagePlus.image.width(), imagePlus.image.height());
    m_eraseRadius = qBound(1.0, m_eraseRadius * std::pow(1.1, steps), maxR);
    update();
    ev->accept();
}

void Label::mouseReleaseEvent(QMouseEvent *ev)
{
    if (m_erasing)
    {
        if (ev->button() == Qt::LeftButton) m_strokeTarget = kNoStroke;
        return;
    }
    if (ev->button() == Qt::RightButton)
    {
        emit rightClicked();
        return;
    }
    if (ev->button() != Qt::LeftButton || !rubberBand || !rubberBand->isVisible()) return;
    rubberBand->hide();
    m_newSel = QRect(origin, ev->pos()).normalized();
    if (m_newSel.width() < 8 || m_newSel.height() < 8)
    {
        // Klick ohne Aufziehen: Grundstellung oder festes Seitenverhaeltnis waehlen
        m_newSel = QRect();
        showRatioDialog(mapToGlobal(ev->pos()));
        return;
    }

    QSize s;
    scale(s.rwidth(), s.rheight());
    const QRectF sel = QRectF(m_newSel).adjusted(-s.width(), -s.height(), -s.width(), -s.height());
    const QSize scaledSz = imagePlus.image.size().scaled(size(), Qt::KeepAspectRatio);
    if (!scaledSz.isEmpty())
    {
        const double sw = static_cast<double>(imagePlus.image.width())  / scaledSz.width();
        const double sh = static_cast<double>(imagePlus.image.height()) / scaledSz.height();
        m_imageCropRect = QRect(
            static_cast<int>(sel.x()      * sw),
            static_cast<int>(sel.y()      * sh),
            static_cast<int>(sel.width()  * sw),
            static_cast<int>(sel.height() * sh)
        ).intersected(imagePlus.image.rect());
    }
    update();
    emit cropChanged();
}

// Auswahl nach einem einfachen Linksklick: Grundstellung oder ein festes
// Seitenverhaeltnis, auf das das Bild erweitert wird. [Abbrechen] aendert nichts.
void Label::showRatioDialog(const QPoint &globalPos)
{
    if (imagePlus.image.isNull())
    {
        restoreBeforeClick();
        return;
    }
    struct Entry { const char *text; QSize ratio; };
    const Entry entries[] = {
        { "Grundstellung", QSize()      },
        { "16:9",          QSize(16, 9) },
        { "9:16",          QSize(9, 16) },
        { "4:3",           QSize(4, 3)  },
        { "3:4",           QSize(3, 4)  },
        { "3:2",           QSize(3, 2)  },
        { "2:3",           QSize(2, 3)  },
        { "1:1",           QSize(1, 1)  },
    };

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Seitenverhältnis"));
    QVBoxLayout *lay  = new QVBoxLayout(&dlg);
    QFormLayout *form = new QFormLayout;
    lay->addLayout(form);

    QComboBox *ratioBox = new QComboBox(&dlg);
    for (const Entry &e : entries)
    {
        ratioBox->addItem(tr(e.text));
        if (e.ratio == m_padBeforeClick)
        {
            ratioBox->setCurrentIndex(ratioBox->count() - 1);
        }
    }
    form->addRow(tr("Seitenverhältnis:"), ratioBox);

    // Reihenfolge = Reihenfolge von PadFill
    QComboBox *fillBox = new QComboBox(&dlg);
    fillBox->addItem(tr("automatisch"));
    fillBox->addItem(tr("erweitern"));
    fillBox->addItem(tr("transparent"));
    fillBox->setItemData(0, tr("Transparent, wenn das Bild selbst transparente Pixel hat, sonst erweitern"), Qt::ToolTipRole);
    fillBox->setItemData(1, tr("Die äußerste Zeile bzw. Spalte wird über den Rand gestreckt"), Qt::ToolTipRole);
    fillBox->setItemData(2, tr("Der Rand bleibt durchsichtig"), Qt::ToolTipRole);
    fillBox->setCurrentIndex(int(m_padFill));
    form->addRow(tr("Hinzugefügter Rand:"), fillBox);

    // In der Grundstellung gibt es keinen Rand. Jede Auswahl wird sofort im
    // Video angezeigt, damit man das Ergebnis vor dem Bestätigen sieht.
    auto applySelection = [&]()
    {
        const QSize ratio = entries[ratioBox->currentIndex()].ratio;
        fillBox->setEnabled(!ratio.isEmpty());
        m_padRatio      = ratio;
        m_padFill       = static_cast<PadFill>(fillBox->currentIndex());
        // Grundstellung: weder Zuschnitt noch Erweiterung,
        // sonst bleibt ein vorhandener Zuschnitt erhalten
        m_imageCropRect = ratio.isEmpty() ? QRect() : m_cropBeforeClick;
        update();
        emit cropChanged();
    };
    connect(ratioBox, &QComboBox::currentIndexChanged, &dlg, applySelection);
    connect(fillBox,  &QComboBox::currentIndexChanged, &dlg, applySelection);
    const PadFill fillBeforeClick = m_padFill;
    applySelection();

    QDialogButtonBox *box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.move(globalPos);

    if (dlg.exec() != QDialog::Accepted)
    {
        m_padFill = fillBeforeClick;
        restoreBeforeClick();           // Abbruch: nichts aendern
        return;
    }
    if (m_padRatio.isEmpty()) m_newSel = QRect();
}

// mousePressEvent() loescht Zuschnitt und Erweiterung schon beim Druecken der
// Taste. Wird der Dialog abgebrochen, muss dieser Stand zurueck.
void Label::restoreBeforeClick()
{
    m_imageCropRect = m_cropBeforeClick;
    m_padRatio      = m_padBeforeClick;
    update();
    emit cropChanged();
}

const QPixmap &Label::scale(int &x, int &y)
{
    static QPixmap scaled;
    scaled = imagePlus.image.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    x = (width()  - scaled.width())  / 2;
    y = (height() - scaled.height()) / 2;
    return scaled;
}

void Label::paintEvent(QPaintEvent *e)
{
    if (imagePlus.image.isNull())
    {
        QLabel::paintEvent(e);
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    int x = 0, y = 0;
    const QPixmap &scaled = scale(x, y);
    p.fillRect(rect(), QColor(Qt::blue));
    const EraseMask mask = m_erasing ? currentMask() : EraseMask();
    if (!mask.isEmpty())
    {
        // Vorschau: radierte Stellen werden durchsichtig
        QImage img = scaled.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter ip(&img);
        ip.setRenderHint(QPainter::Antialiasing, true);
        ip.setCompositionMode(QPainter::CompositionMode_Clear);
        const qreal f = static_cast<qreal>(scaled.width()) / imagePlus.image.width();
        ip.scale(f, f);
        drawEraseStrokes(ip, mask);
        ip.end();
        p.drawImage(x, y, img);
    }
    else
    {
        p.drawPixmap(x, y, scaled);
    }
    p.setPen(QPen(Qt::red, 2));
    p.drawRect(x, y, scaled.width(), scaled.height());
    if (m_erasing)
    {
        // Radierer als dünner Kreis, doppelt für Kontrast auf jedem Untergrund
        const qreal r = m_eraseRadius * scaled.width() / imagePlus.image.width();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::black, 1));
        p.drawEllipse(m_eraserPos, r + 1, r + 1);
        p.setPen(QPen(Qt::white, 1));
        p.drawEllipse(m_eraserPos, r, r);
    }
}

void Label::drawEraseStrokes(QPainter &p, const EraseMask &mask)
{
    p.setBrush(Qt::NoBrush);
    for (const EraseStroke &s : mask)
    {
        p.setPen(QPen(Qt::black, 2 * s.radius, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (s.points.size() == 1) p.drawPoint(s.points.first());
        else                      p.drawPolyline(s.points);
    }
}

Label::EraseMask Label::currentMask() const
{
    return m_globalMask + m_frameMasks.value(imagePlus.index);
}

QRect Label::imageRect() const
{
    const QSize sz = imagePlus.image.size().scaled(size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - sz.width()) / 2, (height() - sz.height()) / 2), sz);
}

QPointF Label::toImage(const QPointF &widgetPos) const
{
    const QRect r = imageRect();
    if (r.isEmpty()) return QPointF();
    const qreal f = static_cast<qreal>(imagePlus.image.width()) / r.width();
    return (widgetPos - r.topLeft()) * f;
}

// Der Radierer kann das Label nicht verlassen: liegt die Maus außerhalb, wird
// sie an den Rand zurückgesetzt
QPointF Label::clampToWidget(const QPointF &pos)
{
    const QPointF clamped(qBound(0.0, pos.x(), qreal(width()  - 1)),
                          qBound(0.0, pos.y(), qreal(height() - 1)));
    if (clamped != pos) QCursor::setPos(mapToGlobal(clamped.toPoint()));
    return clamped;
}

void Label::beginErase(const QPointF &pos)
{
    if (imagePlus.image.isNull()) return;
    m_erasing      = true;
    m_strokeTarget = kNoStroke;
    m_globalMask.clear();
    m_frameMasks.clear();
    // Durchmesser etwa 20 % der kürzeren Bildseite
    m_eraseRadius  = 0.1 * qMin(imagePlus.image.width(), imagePlus.image.height());
    m_eraserPos    = clampToWidget(pos);
    setMouseTracking(true);
    setCursor(Qt::BlankCursor);
    grabMouse();       // Bewegungen außerhalb kommen weiter hier an
    setFocus();        // für Esc
    update();
}

void Label::endErase()
{
    if (!m_erasing) return;
    m_erasing      = false;
    m_strokeTarget = kNoStroke;
    m_globalMask.clear();
    m_frameMasks.clear();
    releaseMouse();
    unsetCursor();
    setMouseTracking(false);
    update();
}

void Label::applyAndEndErase()
{
    const EraseMask global = m_globalMask;
    QHash<int, EraseMask> perFrame = m_frameMasks;
    endErase();
    perFrame.removeIf([](const auto &it) { return it.value().isEmpty(); });
    if (!global.isEmpty() || !perFrame.isEmpty()) emit eraseApplied(global, perFrame);
}

void Label::showEraseMenu(const QPoint &globalPos)
{
    m_strokeTarget = kNoStroke;
    // Für das Menü normale Maus, sonst bekäme es keine Klicks
    releaseMouse();
    unsetCursor();

    QMenu menu(this);
    QAction *discard = menu.addAction(tr("Verlassen ohne Anwenden"));
    QAction *apply   = menu.addAction(m_erasePerFrame ? tr("Verlassen und alle Radierungen anwenden")
                                                      : tr("Verlassen und anwenden"));
    // Nur in der Pause gibt es eine Maske des einzelnen Bildes
    QAction *toAll   = m_erasePerFrame
                           ? menu.addAction(tr("Bildmaske auf alle Bilder des Ausschnitts übernehmen"))
                           : nullptr;
    QAction *stay    = menu.addAction(tr("Radiermodus nicht verlassen"));
    menu.setActiveAction(stay);
    // Esc oder Klick daneben liefert nullptr = im Radiermodus bleiben
    const QAction *clicked = menu.exec(globalPos, stay);

    if (clicked == discard)
    {
        endErase();
        return;
    }
    if (clicked == apply)
    {
        applyAndEndErase();
        return;
    }
    if (toAll && clicked == toAll)
    {
        // Maske des angezeigten Bildes gilt ab jetzt für alle Bilder des Bereichs
        m_globalMask += m_frameMasks.take(imagePlus.index);
    }
    setCursor(Qt::BlankCursor);
    grabMouse();
    setFocus();
    m_eraserPos = clampToWidget(mapFromGlobal(QCursor::pos()));
    update();
}
