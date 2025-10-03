// BarsWidget.cpp
#include "BarsWidget.h"
#include <QPainter>
#include <QDateTime>
#include <algorithm>
#include <cmath>

BarsWidget::BarsWidget(QWidget* parent)
  : QWidget(parent)
{
  setMinimumSize(200, 120);
  setAutoFillBackground(true);
  
  // Initialize trails
  _centroidLTrail.reserve(TRAIL_LEN);
  _centroidRTrail.reserve(TRAIL_LEN);
}

QSize BarsWidget::sizeHint() const {
  return { 480, 240 };
}

void BarsWidget::setBinsRawStereo(const QVector<float>& left,
                                  const QVector<float>& right)
{
  // Store copies
  _left  = left;
  _right = right;
  
  // Calculate centroids (equal weights - simple average of active bins)
  _centroidL32 = calculateCentroid(_left);
  _centroidR32 = calculateCentroid(_right);
  
  // Update trails
  updateTrail(_centroidLTrail, _centroidL32);
  updateTrail(_centroidRTrail, _centroidR32);
  
    _frameCounter++;
    emit snapshotReady(captureSnapshot());

  update(); // trigger repaint
}

Snapshot BarsWidget::captureSnapshot() const
{
    return Snapshot(_left, _right, _centroidL32, _centroidR32, _frameCounter);
}

float BarsWidget::calculateCentroid(const QVector<float>& bins)
{
    if (bins.isEmpty()) return -1.0f;
    
    float weightedSum = 0.0f;
    float totalWeight = 0.0f;
    const float threshold = 0.001f;
    
    for (int i = 0; i < bins.size(); ++i) {
        float weight = bins[i];
        if (weight > threshold) {
            weightedSum += i * weight;  // bin index * amplitude
            totalWeight += weight;      // sum of amplitudes
        }
    }
    
    return (totalWeight > threshold) ? weightedSum / totalWeight : -1.0f;
}

void BarsWidget::updateTrail(QVector<float>& trail, float newCentroid)
{
  if (newCentroid >= 0.0f) { // only add valid centroids
    trail.prepend(newCentroid); // add to front
    
    // Keep trail length limited
    while (trail.size() > TRAIL_LEN) {
      trail.removeLast();
    }
  }
}

void BarsWidget::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true); // enable for smooth dots

  const int n = std::min(_left.size(), _right.size());
  if (n <= 0) {
    p.fillRect(rect(), palette().window());
    return;
  }

  const QRect r = rect();
  const int rowH = r.height() / 2;

  // Bar geometry
  const float gap = 1.0f;
  const float barW = (r.width() - (n - 1) * gap) / float(n);
  const int   barWi = std::max(1, int(std::floor(barW)));

  auto drawRow = [&](const QVector<float>& bins, int y0, int h, const QColor& color)
  {
    float mx = 0.f;
    for (float v : bins) mx = std::max(mx, v);
    if (mx <= 0.f) mx = 1.f;

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

  // Clear background and draw bars
  p.fillRect(r, palette().window());
  
  // Top: Left channel, Bottom: Right channel
  const QRect leftRect(r.left(), r.top(), r.width(), rowH);
  const QRect rightRect(r.left(), r.top() + rowH, r.width(), r.height() - rowH);
  
  drawRow(_left,  leftRect.top(),  leftRect.height(), QColor(80, 220, 120));
  drawRow(_right, rightRect.top(), rightRect.height(), QColor(90, 160, 255));
  
  // Draw centroid trails and current positions
  drawCentroidAndTrail(p, leftRect, n, _centroidL32, _centroidLTrail, QColor(255, 0, 0));
  drawCentroidAndTrail(p, rightRect, n, _centroidR32, _centroidRTrail, QColor(255, 0, 0));
}

void BarsWidget::drawCentroidAndTrail(QPainter& p, const QRect& barsRect, int numBars, 
                                      float centroid, const QVector<float>& trail, 
                                      const QColor& color)
{
  if (numBars <= 0) return;
  
  const float gap = 1.0f;
  const float barW = (barsRect.width() - (numBars - 1) * gap) / float(numBars);
  
  // Helper to convert bin index to x coordinate
  auto binToX = [&](float binIndex) -> float {
    return barsRect.left() + binIndex * (barW + gap) + barW / 2.0f;
  };
  
  const int y = barsRect.center().y();
  
  // Draw trail as connected line segments with tapering thickness/opacity
  if (trail.size() > 1) {
    for (int i = 0; i < trail.size() - 1; ++i) {
      if (trail[i] < 0.0f || trail[i+1] < 0.0f) continue; // skip invalid segments
      
      const float x1 = binToX(trail[i]);
      const float x2 = binToX(trail[i+1]);
      
      // Age factor: 0.0 = newest segment, 1.0 = oldest segment
      const float age = std::clamp(float(i) / float(trail.size() - 2), 0.0f, 1.0f);
      
      // Tapering: newest = thick & opaque, oldest = thin & transparent
      const int alpha = std::clamp(int(255 * (1.0f - age * 0.85f)), 0, 255);
      const float thickness = std::max(1.0f, 4.0f * (1.0f - age * 0.75f));
      
      // Draw line segment
      QColor segmentColor = color;
      segmentColor.setAlpha(alpha);
      
      QPen pen(segmentColor, std::max(1.0f, thickness), Qt::SolidLine, Qt::RoundCap);
      p.setPen(pen);
      p.drawLine(QPointF(x1, y), QPointF(x2, y));
    }
  }
  
  // Draw current centroid (bright dot at the head of the trail)
  if (centroid >= 0.0f) {
    const float x = binToX(centroid);
    
    // Draw a slightly larger dot for current position
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(x, y), 6, 6); // 6px radius for current position
    
    // Optional: add a bright inner core
    p.setBrush(QColor(255, 255, 255, 180)); // white center
    p.drawEllipse(QPointF(x, y), 3, 3);
  }
}