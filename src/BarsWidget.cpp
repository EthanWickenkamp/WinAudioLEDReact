#include "BarsWidget.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

BarsWidget::BarsWidget(QWidget* parent)
  : QWidget(parent)
{
  setMinimumSize(200, 120);
  setAutoFillBackground(true);
}

QSize BarsWidget::sizeHint() const {
  return { 480, 240 };
}

void BarsWidget::setBinsRawStereo(const QVector<float>& left,
                                  const QVector<float>& right)
{
  // Store copies; expect both to be size 32 but handle any size safely.
  _left  = left;
  _right = right;
  update(); // trigger repaint on GUI thread
}

void BarsWidget::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, false);

  const int n = std::min(_left.size(), _right.size());
  if (n <= 0) {
    // Nothing to draw; fill background to keep it clean.
    p.fillRect(rect(), palette().window());
    return;
  }

  const QRect r = rect();
  const int rowH = r.height() / 2;

  // Simple bar geometry with a small gap.
  const float gap = 1.0f;
  const float barW = (r.width() - (n - 1) * gap) / float(n);
  const int   barWi = std::max(1, int(std::floor(barW)));

  auto drawRow = [&](const QVector<float>& bins, int y0, int h, const QColor& color)
  {
    // Auto-scale by per-row max so all bars fit.
    float mx = 0.f;
    for (float v : bins) mx = std::max(mx, v);
    if (mx <= 0.f) mx = 1.f;

    // Optional: a touch of padding so full-scale doesn’t clip the top line.
    const float pad = 0.98f;

    for (int i = 0; i < n; ++i) {
      const float v = std::clamp(bins[i] / mx, 0.f, 1.f) * pad;
      const int   x = r.left() + int(i * (barW + gap));
      const int   bh = std::max(0, int(std::round(v * (h - 1))));
      const int   y = y0 + (h - bh);

      QRect bar(x, y, barWi, bh);
      p.fillRect(bar, color);
    }
  };

  // Clear background.
  p.fillRect(r, palette().window());

  // Top: Left channel (greenish), Bottom: Right channel (bluish)
  drawRow(_left,  r.top(),         rowH, QColor( 80, 220, 120));
  drawRow(_right, r.top() + rowH,  r.height() - rowH, QColor( 90, 160, 255));
}
