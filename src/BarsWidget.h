#pragma once
#include <QWidget>
#include <QVector>
#include <Snapshot.h>

class BarsWidget : public QWidget {
  Q_OBJECT
public:
  explicit BarsWidget(QWidget* parent = nullptr);

  QSize sizeHint() const override;

public slots:
  void setBinsRawStereo(const QVector<float>& left, const QVector<float>& right);

  Snapshot captureSnapshot() const;

signals:
  void snapshotReady(const Snapshot& snapshot);

protected:
  void paintEvent(QPaintEvent* ev) override;

private:
  int _frameCounter = 0;
  QVector<float> _left, _right;
  
  float _centroidL32 = -1.0f;
  float _centroidR32 = -1.0f;

  // Trail data for each channel
  QVector<float> _centroidLTrail;
  QVector<float> _centroidRTrail;
  static constexpr int TRAIL_LEN = 12;

  // Helper methods
  float calculateCentroid(const QVector<float>& bins);
  void updateTrail(QVector<float>& trail, float newCentroid);
  void drawCentroidAndTrail(QPainter& p, const QRect& barsRect, int numBars, 
                           float centroid, const QVector<float>& trail, 
                           const QColor& color);


};