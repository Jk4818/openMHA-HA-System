# openMHA-HA-System

Adaptive steerable beamformer hearing aid (HA) that can be controlled by a cross-platform interface through OSC.
Check out the GUI here: https://github.com/Jk4818/JUCE-Hearing-Aid-GUI

For a quick demo of the entire system in action click **[here](https://youtu.be/tEZiaqk7CAc)**.


# Installation

**The system has only been tested using Linux Ubuntu. It is advised that anyone else do the same.**

Download or  **pull** the GitHub repository. It contains the source files that can be built on your device.
The application in this repo is a modified source copy of openMHA that includes the new head tracking and steerable beamforming functions. To install follow COMPILATION.md in the openMHA folder.

There are additional applications required for the beamformer:
- **[TASCAR](http://www.tascar.org/)**: Software platform for running simulated environments. Used for testing the simulated HA
- **[jmatconvol](https://kokkinizita.linuxaudio.org/linuxaudio/)**: Command line JACK application for convolving TASCAR output receiver to a 4 channel input signal for openMHA
- **[qjackctl](http://www.tascar.org/)**: JACK Audio Connection Kit for routing audio sources within different applications on the Linux machine

# Usage Guide

The **beamformer_live.cfg** is available for use inside **beamformer_setup_files**. It contains a default configuration for the live beamformer. Make sure the custom openMHA source is installed or it won't function.

Only the **osc2ac** and **ac2osc** plugin configuration variables (CV) need to be changed.
Modify the Port, Vars and Host CVs as required to the GUI host device and head tracker.

> The head tracker used during testing was made by supperware. It has easy OSC communication specifications but require a windows or mac OS to run and send/receive messages.

> When running on any device make sure the application has firewall permission for network access, else it will not be able to send or receive any OSC messages.

To run the system, you must route the above applications using JACK as follows:
- TASCAR -> jmatconvol -> openMHA -> system

# Modifying the openMHA application

To change source code of the steerbf plugin, it is located under mha/plugins/steerbf
Make sure to make install in the root openMHA directory.


