#pragma once
#include <QVector>
#include <QMetaType>
#include <QDateTime>

struct Snapshot {
    QVector<float> leftBars;
    QVector<float> rightBars;
    float leftCentroid = -1.0f;
    float rightCentroid = -1.0f;
    QDateTime timestamp;
    int frameNumber = 0;  // for easy reference
    
    Snapshot() = default;
    Snapshot(const QVector<float>& left, const QVector<float>& right, 
                 float leftCent, float rightCent, int frame)
        : leftBars(left), rightBars(right), leftCentroid(leftCent), 
          rightCentroid(rightCent), timestamp(QDateTime::currentDateTime()), 
          frameNumber(frame) {}
};