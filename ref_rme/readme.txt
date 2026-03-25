RME Intelligent Audio Solutions

MADIface XT / XT II / USB / Pro, Fireface UFX+ / UFX II / UFX III, OctaMic XTC, ADI-2 Pro/AE/FS/DAC, ADI-2/4 Pro, Digiface USB / AVB / AES / Dante / Ravenna, USB.IO

Driver for Windows 8 / 10 / 11

Important information: Driver version 0.9845


Installation
-------------

Latest driver:
https://www.rme-audio.de/downloads/driver_madiface_win_xxxx.zip


Latest firmware
https://www.rme-audio.de/downloads/fut_madiface_win.zip


The firmware update is not possible unless the driver had been installed before.

Follow the instructions of the RME Driver Install Wizard. This tool installs/updates all driver files. After that attach the device (in case of first time installation) or reboot (in case of an update) the computer.

----------------------

News, changes and fixes

Attention: This driver is no longer compatible to Windows 7! W7 works with certificates, and since these have expired, a certified driver for W7 can no longer be created. The last certified driver for W7 is version 0.9827.


---------------------

V 0.9845 (06/06/2024)

- Reloading WDM devices did not work as expected with the Digiface Dante.


New in TotalMix FX 1.96:

- Old Digiface/Multiface/RPM: loading Snapshots/Workspaces could be incomplete

- Several fixes for Talkback function: update Trim state, correct Gain settings, Fader operation

- Ext. In Gain setting (horizontal fader) did not work in UFX+, UFX II, UFX III

- Fix: changing sample rates could show wrong fader positions after the change

- Channels not available due to sample rate (SMUX active) are now shown as n.a. at the bottom of the channel (Routing target label)

- Extended Import options for Room EQ Presets with Stereo, to Left Channel, to Right Channel.

- Reworked Preferences window for better overview, operation, and space for even more options (see below) 

- New option Load Room EQs with Snapshot

- New option Save changes regularly (within 1 minute) - saves mixer states during operation, slightly delayed

- Show active Loopback mode in respective Input channel (small LED symbol)

- PEQ: any frequency value between 20Hz and 20000Hz can now be entered for EQ (no conversion to increments as with adjustment by mouse)

- New mismatch messages with improved text and TotalMix/Device buttons instead of Yes/No

- When removing the device the last mismatch message is exit automatically

- Improved TotalReset: also displays all subwindows (Control Room, Sidebar)

- ARC USB: Show device names in ARC USB dialog box header and mixer settings windows

- ARC USB: new option Room EQ On/Off for Main Out

- ARC USB: new setting 'Usage of PC connected ARC USB' with the options Shared (Default), Ignore PC connected ARC USB, Disable when device is offline.

- ARC USB: Save and Load dialog pointed to Snapshots instead of .tmarc files
 
- TM FX now signals the last selected submix to the network remote (for new option Follow Submix in TM FX Remote)

-  Save Workspace writes to Recent list; no stationary flag set

-  Magic Mouse optimization (option to disable tracking)

- Improved limitation handling of max number of character inputs (27)


OSC

- OSC crash fixed (no descriptor found)

- Cue via OSC restricted: does not allow switching to Main Out and Speaker B anymore

- OSC scaling now switchable to correct maximum value for Gain, EQ band type, Room EQ Scale, Expander Threshold,

- Conversion of page 4 for Room EQ (old page 4 invalid, not functional anymore)

- Sending of 0 for non-existent elements

- Increase crash safety for unexpected data 

- Settings, OSC Tab: Added compatibility mode option to disable newer OSC functions of TM FX 1.96.

- Added status text for DURec functions

- New: Signalling Pause state by 'flashing'

- The DURec buttons, state and record time information is no longer repeatedly sent, only on changes


The updated OSC commands are available at the usual link:
<http://www.rme-audio.de/downloads/osc_table_totalmix_new.zip>


----------------------

V 0.9843 (06/06/2024)

- Supports USB.IO module for Direct Out Prodigy and Maven

- Supports MADIface XT II


New in TotalMix FX 1.95:

- Supports USB.IO module

- Supports MADface XT II (requires DSP firmware 55 or higher)

- Crossfeed settings were not applied to Fireface 802 FS

- Small fix for device index display in device selection Dropdown

- Corrected warning dialog for firmware version of Fireface 802, 802 FS and HDSPe MADI FX

- ARC USB: Adjustment of gain control of special channels (e.g. Instrument 3/4 of UCX II)

New in TotalMix FX 1.94:

- Room EQ for Fireface 802 FS, HDSPe MADI FX 2.0 and MADIface XT II

- Bugfix Listenback DIM for Fireface UFX+, UFX II and UFX III


-------------------------------



V 0.9840 (05/03/2024)

- When the sample rate was changed, the no longer valid WDM devices were not reliably deleted

- Settings dialog: reworked GUI for higher Windows compatibility

- Settings dialog: added Hi-DPI support

- Settings dialog: After a restart of the Windows Explorer the icon of the Settings dialog will be shown again in the Task Bar's notification area

----------------------

V 0.9833 (01/23/2024)

- Input Status in Settings dialog of UFX II did not change between ADAT and SPDIF

- Digiface Dante: Changing the number of WDM devices now happens immediately

----------------------

V 0.9831 (01/08/2024)

- Settings dialog supports independent use of AES, ADAT1 and ADAT2 as 3-way AES/SPDIF I/O for UFX II and UFX+. Requires firmware update.

New in TotalMix FX 1.93:

- Numerous minor bug fixes and improvements

- ROOM EQ for UFX II, UFX III, UFX+. Requires DSP firmware update.

- 3-band PEQ: Extended filter functions and Q settings for UFX II, UFX III, UFX+. Requires DSP firmware update.

- New options in Options - Reset Mix: Total Reset without Room EQ, Clear channel effects without Room EQ.

New since TotalMix FX 1.86:

- Improved, fixed and enhanced hide channel and routing menu functions



V 0.9827 (09/13/2023)

- Changed names of 2 Pro and 2/4 Pro MIDI Port for ADI-2 Series Remote

- TotalMix FX is no longer installed for the ADI-2/4 Pro SE


New in TotalMix FX 1.85:

- UCX II: Mute did not work when Phase invert was active

- Fix: PEQ settings allowed invalid entries via keyboard in specific cases

- Talkback did not work in TM FX DAW mode

- Changed behavior for old ARC with UFX and others: activating Speaker B the Main Out of the device is temporarily switched, resulting in the same resolution (0.5dB steps with slow adjustment) for both outputs, and display of the volume setting at the device.

- Added the option 'Volume (TotalMix)' in Settings, which enables volume control for the old ARC by TotalMix (without the 0.5dB steps on slow adjustment, but deterministically related to the mechanical encoder position).

- Tab ARC and Function Keys: with UFX III and UCX II the upper section is renamed to 'Function Keys (Device)'

- Fixed issue with a limitation of MIDI channels

- Added device dependent limits for the mic gain range


-----------------------------

V 0.9824 (06/22/2023)

- Adds MIDI Port for ADI-2 Series Remote

- Supports upcoming firmware versions of ADI-2/4 Pro SE

New in TotalMix FX 1.84:

- Fix: since v 1.80 TM FX would hang on fresh first-time installations

-----------------------------

V 0.9821 (05/23/2023)

Includes TotalMix FX 1.83

----------------------------

V 0.9819 (05/04/2023)

New in TotalMix FX 1.81:

Fix: Mono Cue bug in DAW mode with UFX+

Added: DURec keys of UCX II now available in ARC USB menu

Changed: the Undo of DSP data uploads to the device has been completely re-written to make it bullet-proof 

------------------------------

V 0.9818 (05/02/2023)

- Bug fix audio distorted when using more than one interface (since 0.9815)

------------------------------

V 0.9817 (04/28/2023)

- Bug fix UFX II and UFX+ AES output: since 0.9812 the channel status was fixed to 'Non-Audio'.

------------------------------

V 0.9815 (03/28/2023)

- Completely reworked internal multiclient handling. Reason: the option WinRT MIDI in Cubase 12 caused a crash.


V 0.9812 (03/17/2023)

- Supports the UFX III based on firmware USB 19 DSP 6

- Missing letter added in Settings dialog (Digiace)


New in TotalMix FX 1.80:

- Supports the UFX III

- Improved: Crash detection, to ensure the last saved state is loaded after a crash

- Bug fix Digiface Ravenna: MADI channels were shown as Ravenna channels when Names option was active

- Improved: switching the Digiface Ravenna from USB 2 to USB 3 did not show the additional channels without reboot

- Added support for 75 dB mic gain for the Digiface AES

- New function Offline-Device Setup (Optionws): allows to use TotalMix FX without connected interface, including load/edit/save of Setups and Workspaces

- New function Color in Channel Layout: the channel name can be white or shown in 7 other colors

- Change: Key focus is now locked when in background after start to avoid unintentional configuration changes

- Bugfix Network Remote Settings: Deletion of a user did not work correctly

- Improved: Opening of Network Remote Settings device-dependent, avoids collision with mixer settings

- Improved: Loading of user list in Network Remote Settings

- New ARC USB: can now control the makeup gain of the mic channel's Dynamics section

- New ARC USB: all encoder functions are now also available as 'Push' mode (only active as long as the button is pressed)

- New PFL mode: When in PFL mode the global Solo button (upper right) will switch off all active PFLs

- Fix: in rare cases connecting the audio interface could exit TotalMix


- Extended OSC support. The updated OSC table is here:

http://www.rme-audio.de/downloads/osc_table_totalmix_new.zip

The OSC table zip includes a new touchOSC template that showcases all functions.

New in Page 1: CUE control as channel dependent function, control of link speaker B, and the direct selection of a channel in Page 2 as a start offset of Page 1.

New in 'none paged' page: added setOffsetInBank and loadQuickWorkspace (these are no new functions)

- Various more performance improvements and fixes


-----------------

V 0.9808 (02/06/2023)

- Start/Stop behaviour improved

- Internal: new relative paths following Microsoft rules

- Added workaround for programs that not understand relative paths for the ASIO driver


New in TotalMix FX 1.79:

- Supports the HDSPe series with DriverKit compatible firmware

- UCX II: supports up to +24 dB Gain for Inst and Line inputs (requires firmware USB V42 or higher)


-----------------

V 0.9801 (11/17/2022)

- Added support for new devices

New in TotalMix FX 1.78:

- Added support for new devices

- Bug fix: in rare cases the upload of UCX II mixer data did not work reliable

- Bug fix: muted routings to mono output channels caused a mismatch message with the UCX II

----------------------

V 0.9794 (09/09/2022)

- WDM: Assigning a device to be Speaker sometimes failed with the device removed

----------------------

V 0.9787 (09/05/2022)

- Adds support for Digiface Ravenna and ADI-2/4 Pro

- ASIO: Various latency values corrected

- Improved handling of Surprise Removal events


New in TotalMix FX 1.77:

- Added support for new devices

- Improved handling of order in multi-device operation

- Improved mismatch message (correct device name)

- Bugfix Babyface encoder communication

- Improved transfer/handling of large mixer data

- Bug fix for HDSPe MADI FX, MADIface XT and MADIface Pro: Reverb Level Meter were not updated (Windows only).

---------------------------------

V 0.9747 (12/09/2021)

- Enhanced Settings Dialog for Digiface Dante. Requires firmware 45 and update of the Dante firmware.

New in TotalMix FX 1.75:

- ARC USB: Mic Gain settings did not work correctly with UC and UCX II

- Fix: Since version 1.74 the output routing of Digiface Dante and Digiface AVB did not work

---------------------------------

V 0.9735 (09/20/2021)

- AC-3/DTS digital playback is now possible

New in TotalMix FX 1.73:

- Touch screen: The right mouse button gesture now works for the output channels

- Exclusive Solo on mirrored outputs (including Speaker B) did not work correctly

- Mute Phones 1 to 4 has been added to the drowdown list of the ARC USB button assignments

- DURec: The playback channel list (drop down menu)  now indicates non-existing channels. Example: A DURec recording of 6 channels will show channels 7 to x in that list in brackets. This way it is easy to see how many channels are really there to be assigned for playback. For full compatibility and universal operation all channels can still be selected.

- Improved detection of the shift key in TM FX. It now works most of the time, even when another app is in focus/foreground.

- ARC USB Settings: new Default encoder function options Volume Main (Device), Volume Main (0.5 dB), Volume Main (0.1 dB).


Mode                    Shift ARC IF    Shift ARC PC*   Steps
Volume Main (Device)    No              Yes             0.5 dB, accelleration*
Volume Main (Default)   Yes             Yes             0.5/1.0/1.5/2.0/2.5/3.0
Volume Main (0.5 dB)    Yes             Yes             0.5 dB
Volume Main (0.1 dB)    Yes (0.5 dB)    Yes (0.5 dB)    0.1 dB

The table shows that if the ARC USB is connected directly to the interface (UFX+, UFX II), for technical reasons the shift key does not work in 'Volume Main (Device)' mode. Shift causes a change to 0.1 dB steps (fine mode), except in Volume Main (0.1 dB) mode, there it changes to 0.5 dB. 

* UFX+ and UFX II: In this mode with the ARC USB connected to the PC, for technical reasons the steps are the same as in Default mode.

---------------------

V 0.9735 (04/13/2021)

- unpack option /manual works again

New in TotalMix FX 1.72:

- Fixed a multi-device state save issue introduced in 1.70 B5

- Fixed an internal messaging error that could cause the current state not to be saved on shutdown in certain setting configurations (for example Host server active or MIDI in use)

- Improved multi-device support to ensure the numbering in Settings dialog and TM FX are synced in any case, including manual sort-order changes, and that ASIO Direct Monitoring uses the correct interface.

- Some text improvements (Locator, flashing settings of the Digiface Dante…)

- Preferences, Do not load: Selecting ‘Control Room Settings’ now includes the CRM mute states

- Loading Quick Workspaces via OSC is now supported. This is a non-paged function, see the updated OSC table, http://www.rme-audio.de/downloads/osc_table_totalmix_new.zip

---------------------

V 0.9734 (11/24/2020)

New in TotalMix FX 1.67:

- Internal optimizations

---------------------

V 0.9728 (10/20/2020)

- Multi-interface setup ASIO: Changing the sort order (Settings dialog, About) caused ADM to address the wrong interface. Requires TotalMix FX 1.70 (via forum) or higher for full functionality.

New in TotalMix FX 1.67:

- Version 1.66 did not recognize the current Fireface UC (generation 2) anymore

-----------------------

V 0.9723 (09/18/2020)

- ASIO: Optimized memory check routine for full compatibility and performance as with version 0.9721

New in TotalMix FX 1.66:

- Extended mixer for Digiface Dante (maximum number of nodes raised from 2048 to 4096). Requires firmware 37 for full node count.

- Only Thunderbolt UFX+: ASIO Direct Monitoring did not work with Thunderbolt.

- Optimized shut down procedure. This is another attempt to reduce the harmless but annoying crash message of TM FX that some users experience when shutting down the computer.

-----------------------

V 0.9722 (08/18/2020)

- Bug fix ASIO: Pause mode could cause BSOD on Surprise Removal with Windows 10 2004.

-----------------------

V 0.9716 (03/06/2020)

- Bug fix UFX+: Clock Source MADI did not work (since 0.9700)

-----------------------

V 0.9700 (11/12/2019)

New in TotalMix FX 1.63:

- Babyface: OSC SetBankStart did not work as expected 

- UFX II / UFX+: fixes mute issue of Speaker B when assigned to An 1/2

- UFX II / UFX+: fixes pan issue when swapping channels of a software playback via mono channel pans and Snapshots

- UFX II / UFX+: Hides Hi/Low button of Phones 10 in mono mode

- ARC USB: new option 'Push-off or-on' for Talkback

- Digiface AVB: increased number of channels in Quad Speed mode to 64


--------------------

V 0.9685 (06/25/2019)

News, changes and fixes

- Added support for MIDI over MADI for MADI I/O of Digiface Dante (requires firmware 36)

New in TotalMix FX 1.61:

- ARC USB: New option Push-Off

- ARC USB: Refresh problem of Key settings fixed

- ARC USB: Key settings cleanly initialized (change push to encoder)

- ARC USB: Phantom and Inst did not light up the respective button when active

- Digiface Dante: Number of channels increased to 64 at 192 kHz

- Improved OSC start behaviour with delayed WiFi detection

- Fireface 800: Flashes all 28 channels as current state into the unit

- New command line option /nc prevents the confirmation dialog when loading workspaces via command line


----------------------


V 0.9684 (04/23/2019)

- Bug fix Digiface Dante WDM recording: the WDM record devices 5/6 and 7/8 received the signal 5/6, all channels above 7/8 were shifted by 2 channels.

----------------------

V 0.9681 (02/25/2019)

New in TotalMix FX 1.6:

- Settings for External Input and current PFL mode are no longer changed per Snapshot.

- Solo/PFL have now priority over the Mute state of an input channel. Note: This feature is not available for UFX II and UFX+.

- Settings, Mixer: New option 'Exclusive Solo/PFL Mode'

- Settings, Mixer: New option '2-Row Mode with only Input in first row'

- Settings, Aux Devices: Increased number of Aux device channel banks to 24, so that MADIface XT and HDSPe MADI FX can control an Octamic XTC even on MADI port 3.

- Preferences, Mixer Views: New option 'Store channel open/close state in Layout Preset'.

- Preferences, Snapshots: new 'do not load' options 'Main Volume, Main/Phones Volume, Control Room Settings'.

- Preferences, New Option 'Store Settings for all users'. So far every user had his own TM FX state/settings. This option now allows to have the same settings for all users.

-  Added full support for multi-user operation. On a user change TM FX will automatically close and reopen. According to the above option it then loads either specific settings for the current user, or the same settings for all users.

- Menu File: New option 'Preload all snapshots'. Loads the 8 snapshots of a workspace into the Snapshot section, but does not activate any of them. Other settings within the workspace file (window position etc.) are ignored.

- Matrix: Shift + click to change phase, Ctrl + click to mute (or double click) on the vertical channel name columns.   

- ARC USB: added new mode 'Push-or'. Allows for example to activate Talkback with a button and the footswitch without blocking each other.

- ARC USB, Usage: supports SysEx mode (requires firmware update of the ARC USB). Additional option to disable the former mode. Firmware download:

http://www.rme-audio.de/download/fut_arcusb_mac_v7.zip


Fixes:
- A locked user interface still allowed the double-click on a fader

- In rare situations the Mismatch message could cause a crash.

- Fixes different levels (0.05 dB) in level meters


------------------------------

V 0.9680 (11/05/2018)

- Minor improvement with storing WDM format

- Digiface Dante: Fixed the Settings dialog in USB2 mode


------------------------------


V 0.9673 (06/23/2018)

- WDM format restricted to 2-channel (stereo) and up to 7.1-channel (Multi-channel). Therefore the current setting stays even after reboot.

- Fix for Digiface USB WDM Phones Out at Double and Quad Speed

- Added support for Digiface AVB and Digiface Dante 

------------------------------


V 0.9654 (05/07/2018)

- ASIO I/O latency values for ADI-2 Pro and DAC corrected

New in TotalMix FX 1.50 (3):

General:

- Fixed Windows shutdown problem
- Increased number of Aux Devices to 16 
- Fixed potential crash cause when the number of channels change
- Removed left-over context menu entries in DAW mode
- Supports TotalMix Remote via TCP/IP
- Fix Peak Hold Time setting
- Try to recreate Z-order of multiple TM FX windows
- Fix TM FX crash when ADM is active and sample rate is changed to DS/QS
- Fix ADM problem of UFX+ at double speed

ARC USB:

- ARC USB is freed after 3 seconds when not assigned to any interface (use it as remote for other tasks)
- Supports Shift key for fine fader control
- Wheel rotation is now locked to the fader's travel position, unless going over the top and bottom
- Added storing/loading of ARC USB presets
- Toggle TM FX open/close via hotkey or ARC USB
- Added Inst and +48V for ARC USB buttons

TotalMix Remote:

- New option to remote TotalMix FX via TCP/IP (Ethernet, WiFi). See included PDF. Requires additional download of TotalMix Remote for Windows, MacOS or iOS.


------------------------------

News, changes and fixes

V 0.9653 (03/29/2018)

- ASIO: fixed a crash with Cubase after Surprise Removal

-------------------------------

V 0.9650 (03/09/2018)

- General: fixed memory leak with the Surprise Removal function

-------------------------------

News, changes and fixes

V 0.9634 (02/01/2018)

- UFX+ USB 3.0: The option 'ISO Record' in the Settings dialog did not switch from Bulk mode to isochronous streaming with the UFX+. This is necessary with current AMD computers (Ryzen), as their Bulk mode is fragile for audio transmissions.

- ADI-2 DAC: The record channels have been renamed from Analog to SPDIF.

-------------------------------

V 0.9619 (01/08/2018)

- Using more than one interface, the order of the ASIO channels can now be defined in the Settings dialog, tab About, with two different sorting methods. This feature brings them into the desired order, and also prevents random changes, for example after reboot.

- Change in the Installer to prevent ASIO rights problems with non-admin accounts

-------------------------------

V 0.9616 (11/27/2017)

Adds support for the ADI-2 DAC.

-------------------------------

V 0.9613 (10/04/2017)

The option 'Limit ASIO to 32 channels' was constantly active in some cases.

-------------------------------

V 0.9612 (08/25/2017)

This driver update supports the latest flash update tool that updates the OctaMic XTC to v 47/26.

----------------------

V 0.9611 (07/06/2017)

TotalMix FX 1.43:

Fixes:
- ARC USB: re-plugging the ARC USB could cause a jump in volume
- ARC: Speaker B fader behavior in unlink mode

For ARC USB:
- Added Recall Volume (Main Out)
- Added Play - Next File, Play - Previous File (DURec UFX+/UFX II)

Supports DURec Update for UFX+ and UFX II (requires firmware update as well):
- Added new play modes: Repeat single, Repeat all, Single next (plays the current track and switches to the next one without playing it).
- Added new transport buttons: Previous file / Next file. With Pause active Previous/Next works without starting playback.
- The list of files on the medium is no longer blocked during record/playback. During playback this also allows to pre-select the next file to be played.
- The DURec Info dialog entry 'Remaining Time' now shows the same info as the unit as 'Remain (Rec.)' - the remaining time of the current recording (which is limited to 100 files), not the theoretical max record time of the medium.

An updated Wave File Batch Processor 1.2 is available on the RME website, handling the new DURec meta data as described in the updated manuals of UFX+ and UFX II. 

Previous changes in TotalMix FX 1.42:

Fixes:
- Digiface USB: in the third row the over indicator did not work
- Basic and Advanced Remote: stutter/jitter behavior when changing volume fixed

New:
- The currently loaded Channel Layout is now visible because the respective button stays lit
- Extended OSC commands as described in this thread, post 6:
<https://www.forum.rme-audio.de/viewtopic.php?id=25205>


----------------------

V 0.9610 (04/27/2017)

- ADI-2 Pro: Bugfix ASIO native record did not work with some programs


----------------------

V 0.9609 (03/30/2017)

- MADIface USB: Switching off TMS removes the information from MADI input channel 2

New in TotalMix FX 1.41:

- Digiface USB: in the third row the over indicator did not work

----------------------

V 0.9606 (02/17/2017)

- ASIO native record/playback for DSD added


----------------------

V 0.9590 (12/15/2016)

- WDM device names fixed

- WDM record devices SPDIF and AES were swapped

- Removed Microsoft sAPO at 352.8/384 kHz - not supported from Windows

- Added WASAPI format 32 bit

- Removed obsolete 4/6/8 channel formats

- ASIO buffer size 32k did not work correctly


MADIface Pro:

- Input Status in Settings dialog showed wrong channel count in Double Speed mode

- WDM: Playback channels no longer activated as Speaker as default


General:

- New option 'Limit ASIO to 32 channels' on the About tab

- Fix crash of ASIO on surprise removal


New in TotalMix FX 1.40:

- Improved operation and robustness of the MIDI I/O functionality (remote), including device removal and sleep/wake scenarios.

- The name of the currently loaded Workspace and Snapshot file, and also the currently loaded Quick Workspace, are shown in the title bar.

- Added full support for multi-interface operation of the upcoming ARC USB.

- Changed some menu entries to better match their meaning/function.

- Gain settings text fixed

- Fixed the 'FX Send' text in various dialogs

- Fixed a bug with scrambled text in routing menus when using Trim and selecting a different submix

- DAW mode dialog: replaced Abbrechen with Cancel.

- Fix: when deactivating the Aux Device remote control with an XTC and the Tools panel is open, Gain was reset to 0.

- Aux Devices on UFX+ now covers all 8 MADI 8-channel groups

- Fixed an issue with Mute groups and Speaker B mutes on Mains/Speaker B.

- Global Mute display issue: on UFX+ only mutes on the output channels caused a brighter state

- Global Mute display issue: wrong color scheme at 135% zoom

- Connecting a unit TM FX windows now return to their former zoom state

- Changing the main outs via OSC remote was not communicated to the remote when 'Submix linked to MIDI control' was off.

- Several smaller fixes and improvements for smooth operation.


----------------------

V 0.9583 (11/10/2016)

- Improved compatibility of installer and driver files to any OS version

- Digitally signed for Windows 7, 8 und 10 (including Redstone)

- Bugfix ASIO: ADAT channels were not reduced at Double and Quad Speed


New in TotalMix FX 1.35:

- The minimum delay time for Echo has been reduced to 0 for MADIface Pro. Note that the lowest real delay is the current ASIO buffer size x 2, because this devices render the FX on the host CPU.

- General improvement on stability when TM FX is already running and a device is switched on

- Phantom power and mic gain can now be controlled via page 1 when OSC is set to use 8 channels

- Cue via remote control now works even when 'Cue/PFL to' is not set to Main (but Phones etc)

- OSC settings could crash when many mono channels were configured

----------------------

V 0.9574 (10/11/2016)

- Bugfix UFX+ ASIO: ADAT Channels were not reduced at Double and Quad Speed.


----------------------

V 0.9573 (10/05/2016)

- Reupload. Additionally signed for Windows 7 and 8.


----------------------

V 0.9573 (09/22/2016)

- Various improvements and fixes for the ADI-2 Pro and UFX+ release

- Supports the OctaMic XTC (WDM / ASIO). Requires firmware version 46!

- Signed for Windows 10 Redstone

----------------------

V 0.9554 (07/22/2016)

- Adds support for the ADI-2 Pro and the UFX+

- Settings dialog could not be started from an external ASIO host


New in TotalMix FX 1.31:

- Improved Mono output support for ASIO Direct Monitoring.

- Support for more than 8 channels in OSC. An additional setting now allows to select 8 (default), 12, 16 or 24 channels.

- An OSC remote can be assigned to a specific submix.

- DAW mode added. This simplified alternative interface is for anyone performing all monitoring and routing within the DAW software, so not using TotalMix FX. The DAW mode restarts TM FX into a simpler version with just two rows, no playback row, and no mixing faders in the input row. Routing is 1:1 only. Just gain, phantom power control (if existing) and hardware output levels can be set.


----------------------


Copyright RME 09/2024
All rights reserved. Windows 7/8/10/11 are trademarks of Microsoft Corporation. ASIO is a trademark of Steinberg Media Technologies GmbH.

