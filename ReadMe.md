This repository is a QT windows application to take audio loopback from the device and process it into fft bins
The goal is to take processed audio and use it to stream led effects to wled modules

## How it works

### main.cpp
This is what we make the executable out of
It simply shows the MainWindow.h file

### MainWindow

Uses 3 QT classes: QPushButton, QLabel, QProgressBar
Uses 3 of our classes: AudioCapture, AudioProcessor, BarsWidget
