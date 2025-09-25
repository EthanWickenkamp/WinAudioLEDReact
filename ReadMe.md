This repository is a QT windows application to take audio loopback from the device and process it into fft bins
The goal is to take processed audio and use it to stream led effects to wled modules

## How it works


### External Libraries

#### miniaudio - AudioCapture
just a header file
allows us basic audio controls
creates loopback device and streams into callback function

#### kissfft - AudioProcessor
simple fft library
input sample rate, sample size, overlap size
give a spectrum and magnitude?


### main.cpp
This is what we make the executable out of
It simply shows the MainWindow.h file

### MainWindow

Uses 3 QT classes: QPushButton, QLabel, QProgressBar

Uses 3 of our classes: AudioCapture, AudioProcessor, BarsWidget, UDPSrSender

Here we create QT objects like buttons and windows with layout

Then we create our own classes objects and move them to QT threads

We then connect all of the signals and slots between the our objects

main.cpp starts the window but the application is made in MainWindow


### AudioCapture
Set up miniaudio loopback device and emit audio frames

### AudioProcessor
Receive audio frames and perform fft to send to bars widget and udpSRSender

### BarsWidget
Turn frequency bin values into visual with QTPaint and more

### UDPSrSender
Create a WLED SR packet with our outputs from processor


## Current State / Goals

Currently we can analyze loopback audio and turn it into 32 bins ranging from 20-18000 hz
The 32 bins are then smashed into 16 bins by averaging adjacent bins this is temporary

Ideally we can do calculations on instances of the 32 bins
We are looking for percussive and harmonic sounds
- Percussive can be seen by sudden jumps over multiple frequencies
- Harmonic is a sustained tone at a more specific frequency sometimes changing slowly

We could also look at the shape of the bars as a whole
Or the frequency of certain bars spiking

Finally we can create light effects dependent on whatever we can collect from the 32
to create an edited 16 bins with the goal of best representing sounds on premade effects

Can combine these two to create triggers where led packets not sound packets take over from the standard SR effects

Onset detection is when we recognize a new drum, key, vocal with a sudden dramatic frequency change

Spectral flux is how much the frequency spectrum changes between 2 frames

Pitch classes means 440hz and 880hz contribute to the same A note bin, octaves


