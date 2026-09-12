#include "SpriteImportDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

// ─── SpriteFramePreview ──────────────────────────────────────────────────────

SpriteFramePreview::SpriteFramePreview(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 320);
    setMouseTracking(true);
}

void SpriteFramePreview::setCell(const QPixmap &cell)
{
    m_cell = cell;
    update();
}

void SpriteFramePreview::setFrameSize(const QSize &size)
{
    if (size.isEmpty() || size == m_size)
    {
        return;
    }
    m_size = size;
    update();
}

bool SpriteFramePreview::isLandscape() const
{
    return m_size.width() >= m_size.height();
}

// Die lange Seite bekommt immer die volle verfügbare Breite bzw. Höhe, damit
// beim Ziehen nur die kurze Seite wandert und das Bild nicht springt.
int SpriteFramePreview::longExtent() const
{
    return qMax(1, qMin(width(), height()) - 2 * kMargin);
}

QRect SpriteFramePreview::frameRect() const
{
    const int longPx   = longExtent();
    const int longVal  = qMax(1, qMax(m_size.width(), m_size.height()));
    const int shortVal = qMax(1, qMin(m_size.width(), m_size.height()));
    const int shortPx  = qMax(1, qRound(double(longPx) * shortVal / longVal));

    const QSize drawn = isLandscape() ? QSize(longPx, shortPx) : QSize(shortPx, longPx);
    return QRect(QPoint((width() - drawn.width()) / 2, (height() - drawn.height()) / 2), drawn);
}

bool SpriteFramePreview::overHandle(const QPoint &pos) const
{
    const QRect r = frameRect();
    if (isLandscape())
    {
        return qAbs(pos.y() - r.bottom()) <= kGrabTolerance
               && pos.x() >= r.left()  - kGrabTolerance
               && pos.x() <= r.right() + kGrabTolerance;
    }
    return qAbs(pos.x() - r.right()) <= kGrabTolerance
           && pos.y() >= r.top()    - kGrabTolerance
           && pos.y() <= r.bottom() + kGrabTolerance;
}

// Der Rahmen sitzt mittig, die gezogene Kante wächst also symmetrisch: der
// Abstand des Mauszeigers zur Mitte ist die halbe kurze Seite.
void SpriteFramePreview::applyDrag(const QPoint &pos)
{
    const int longPx  = longExtent();
    const int longVal = qMax(1, qMax(m_size.width(), m_size.height()));

    const int half     = isLandscape() ? pos.y() - height() / 2
                                       : pos.x() - width()  / 2;
    const int shortPx  = qMax(1, 2 * half);
    const int shortVal = qBound(1, qRound(double(shortPx) * longVal / longPx), longVal);

    emit shortSideDragged(shortVal);
}

void SpriteFramePreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.fillRect(rect(), palette().base());

    const QRect r = frameRect();
    if (!m_cell.isNull())
    {
        p.drawPixmap(r, m_cell.scaled(r.size(), Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation));
    }

    p.setPen(QPen(palette().highlight().color(), 1));
    p.drawRect(r.adjusted(0, 0, -1, -1));

    // Griff an der ziehbaren Kante andeuten
    p.setPen(QPen(palette().highlight().color(), 3));
    if (isLandscape())
    {
        const int cx = r.center().x();
        p.drawLine(cx - 16, r.bottom(), cx + 16, r.bottom());
    }
    else
    {
        const int cy = r.center().y();
        p.drawLine(r.right(), cy - 16, r.right(), cy + 16);
    }
}

void SpriteFramePreview::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && overHandle(event->pos()))
    {
        m_dragging = true;
        applyDrag(event->pos());
    }
}

void SpriteFramePreview::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging)
    {
        applyDrag(event->pos());
        return;
    }
    setCursor(overHandle(event->pos())
                  ? (isLandscape() ? Qt::SizeVerCursor : Qt::SizeHorCursor)
                  : Qt::ArrowCursor);
}

void SpriteFramePreview::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = false;
    }
}

// ─── SpriteImportDialog ──────────────────────────────────────────────────────

const QList<int> &SpriteImportDialog::longSideChoices()
{
    static const QList<int> choices = {256, 512, 1024, 2048};
    return choices;
}

namespace
{
// Bekannte Seitenverhältnisse für die Ergebniszeile
struct NamedRatio { double value; const char *label; };
const NamedRatio kNamedRatios[] = {
    { 16.0 /  9.0, "16:9" }, {  9.0 / 16.0, "9:16" },
    {  4.0 /  3.0, "4:3"  }, {  3.0 /  4.0, "3:4"  },
    {  3.0 /  2.0, "3:2"  }, {  2.0 /  3.0, "2:3"  },
    { 21.0 /  9.0, "21:9" }, {  9.0 / 21.0, "9:21" },
    {  5.0 /  4.0, "5:4"  }, {  4.0 /  5.0, "4:5"  },
    {  1.0,        "1:1"  },
};

QString ratioText(const QSize &size)
{
    if (size.isEmpty())
    {
        return QString();
    }
    const double ratio = double(size.width()) / size.height();
    for (const NamedRatio &r : kNamedRatios)
    {
        if (qAbs(ratio - r.value) / r.value < 0.02)
        {
            return QString::fromLatin1(r.label);
        }
    }
    return QString("%1:1").arg(ratio, 0, 'f', 2);
}
}   // namespace

SpriteImportDialog::SpriteImportDialog(const QPixmap &cell, const QSize &suggestion,
                                       int frameCount, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Originalgröße des Videos festlegen");

    // Startwerte aus dem Vorschlag: Ausrichtung und Verhältnis der Sheet-Zelle
    if (!suggestion.isEmpty())
    {
        m_landscape = suggestion.width() >= suggestion.height();
        const int longVal  = qMax(suggestion.width(), suggestion.height());
        const int shortVal = qMin(suggestion.width(), suggestion.height());
        m_shortSide = qBound(1, qRound(double(m_longSide) * shortVal / longVal), m_longSide);
    }

    auto *hint = new QLabel(
        QString("Der Dateiname enthält keine Größenangabe (nur vier Parameter). "
                "Die Proportionen des Originalvideos lassen sich aus dem Sheet nicht "
                "zurückrechnen und müssen deshalb hier festgelegt werden – am Rahmen "
                "ziehen oder die Felder unten benutzen.\n"
                "%1 Frames, die Vorschau zeigt den ersten.").arg(frameCount), this);
    hint->setWordWrap(true);

    m_preview = new SpriteFramePreview(this);
    m_preview->setCell(cell);

    m_rbLandscape = new QRadioButton("Querformat", this);
    m_rbPortrait  = new QRadioButton("Hochformat", this);
    m_rbLandscape->setChecked(m_landscape);
    m_rbPortrait->setChecked(!m_landscape);
    auto *orientRow = new QHBoxLayout;
    orientRow->addWidget(m_rbLandscape);
    orientRow->addWidget(m_rbPortrait);
    orientRow->addStretch();

    m_longBox = new QComboBox(this);
    for (int choice : longSideChoices())
    {
        m_longBox->addItem(QString::number(choice), choice);
    }
    m_longBox->setCurrentIndex(longSideChoices().indexOf(m_longSide));

    m_shortSlider = new QSlider(Qt::Horizontal, this);
    m_shortSpin   = new QSpinBox(this);
    m_shortSpin->setSuffix(" px");
    auto *shortRow = new QHBoxLayout;
    shortRow->addWidget(m_shortSlider, 1);
    shortRow->addWidget(m_shortSpin);

    m_result = new QLabel(this);

    auto *form = new QFormLayout;
    form->addRow("Ausrichtung:", orientRow);
    form->addRow("Lange Seite:", m_longBox);
    form->addRow("Kurze Seite:", shortRow);
    form->addRow("Ergebnis:",    m_result);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_preview, 1);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(m_rbLandscape, &QRadioButton::toggled, this, [this](bool on)
            {
                if (m_updating) return;
                m_landscape = on;
                refresh();
            });
    connect(m_longBox, &QComboBox::currentIndexChanged, this, [this](int)
            {
                if (m_updating) return;
                const int previousLong = m_longSide;
                m_longSide = m_longBox->currentData().toInt();
                // Verhältnis beim Wechsel der Auflösung beibehalten
                m_shortSide = qBound(1, qRound(double(m_shortSide) * m_longSide / previousLong),
                                     m_longSide);
                rebuildShortRange();
                refresh();
            });
    connect(m_shortSlider, &QSlider::valueChanged, this, [this](int value)
            {
                if (m_updating) return;
                setShortSide(value);
            });
    connect(m_shortSpin, &QSpinBox::valueChanged, this, [this](int value)
            {
                if (m_updating) return;
                setShortSide(value);
            });
    connect(m_preview, &SpriteFramePreview::shortSideDragged, this,
            [this](int value) { setShortSide(value); });

    rebuildShortRange();
    refresh();
}

QSize SpriteImportDialog::frameSize() const
{
    return m_landscape ? QSize(m_longSide, m_shortSide)
                       : QSize(m_shortSide, m_longSide);
}

void SpriteImportDialog::rebuildShortRange()
{
    m_updating = true;
    m_shortSlider->setRange(1, m_longSide);
    m_shortSpin->setRange(1, m_longSide);
    m_updating = false;
}

void SpriteImportDialog::setShortSide(int value)
{
    const int clamped = qBound(1, value, m_longSide);
    if (clamped == m_shortSide)
    {
        return;
    }
    m_shortSide = clamped;
    refresh();
}

void SpriteImportDialog::refresh()
{
    const QSize size = frameSize();

    m_updating = true;
    m_rbLandscape->setChecked(m_landscape);
    m_rbPortrait->setChecked(!m_landscape);
    m_shortSlider->setValue(m_shortSide);
    m_shortSpin->setValue(m_shortSide);
    m_updating = false;

    m_preview->setFrameSize(size);
    m_result->setText(QString("%1 × %2   (%3)")
                          .arg(size.width()).arg(size.height()).arg(ratioText(size)));
}
