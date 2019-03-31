// Copyright (c) 2019, Jon Wheeler
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "HSApplication.h"
#include "OC_scales.h"
#include "braids_quantizer.h"
#include "braids_quantizer_scales.h"
#include "envgen/envelope_generator.h"
#include "hem_arp_chord.h"

#define MELODY_MAX_SCALE 63
#define MELODY_PAGESIZE 5

class MelodyMaker : public HSApplication {
   public:
    void Start() {
        _cvRegister = random(0, 65535);
        _gateRegister = random(0, 0xffff);

        _root = 0;                            //C
        _scale = OC::Scales::SCALE_SEMI + 1;  // Ionian/Major
        _gateRegisterLength = 16;
        _gateProb = 0;
        _gateProbLocked = true;
        _cvRegisterLength = 16;
        _cvProb = 0;
        _cvProbLocked = true;
        _cvRange = 12;
        _octave = 1;
        _output4Mode = 2;

        // initialise envelope params
        for (int8_t i = 0; i < 4; i++) {
            _envelopeStage[i] = HEM_EG_NO_STAGE;
            _envelopeStageTicks[i] = 0;
            _envelopeGated[i] = 0;
        }

        _selectedMenuIndex = 0;
        _topMenuIndex = 0;

        _quantizer.Configure(OC::Scales::GetScale(_scale), 0xffff);
        _quantizer.Init();
    }

    void Resume() {
    }

    void Controller() {
        // CV 1 bi-polar mod over range
        _moddedCvRange = _cvRangeModSource > 0 ? constrain(_cvRange + Proportion(DetentedIn(_cvRangeModSource - 1), HEMISPHERE_MAX_CV, 24), 1, 24) : _cvRange;

        // bi-polar modulation over Gate probability
        _moddedGateProb = _gateProbModSource > 0 ? constrain(_gateProb + Proportion(DetentedIn(_gateProbModSource - 1), HEMISPHERE_MAX_CV, 100 + 3), 0, 100) : _gateProb;

        // bi-polar modulation over CV probability
        _moddedCVProb = _cvProbModSource > 0 ? constrain(_cvProb + Proportion(DetentedIn(_cvProbModSource - 1), HEMISPHERE_MAX_CV, 100 + 3), 0, 100) : _cvProb;

        if (Clock(0)) {
            StartADCLag(0);
        }

        if (Clock(3)) {
            StartADCLag(3);
        }

        if (EndOfADCLag(0)) {
            int gateProb = _gateProbLocked && !GateProbUnlockedByInput() ? 0 : _moddedGateProb;

            AdvanceGateRegister(gateProb);

            bool gateHigh = _gateRegister & 0x01;

            // Get a new note if there is a new high gate
            if (gateHigh && (gateHigh != _lastGateHigh)) {
                int cvProb = _cvProbLocked && !CVProbUnlockedByInput() ? 0 : _moddedCVProb;
                AdvanceCVRegister(cvProb);
                if (_cvSource == 0) {
                    _currentMelodyNote = GetNote(_cvRegister, _cvRegisterLength, _moddedCvRange, _octave, true);
                } else if (_arpMode == 0) {
                    _currentMelodyNote = GetArpNote(_octave, false, true);
                }
            }

            if (_cvSource == 1 && _arpMode == 1) {
                _currentMelodyNote = GetArpNote(_octave, false, true);
            }

            Out(0, _currentMelodyNote);

            _lastGateHigh = gateHigh;
        }

        if (EndOfADCLag(3)) {
            // output a clocked bassnote
            if (Gate(3)) {
                Out(1, _cvSource == 0 ? GetNote(_cvRegister, _cvRegisterLength, 6, _bassOctave) : GetArpNote(_bassOctave, true));
            }
        }

        bool gateHigh = _gateRegister & 0x01;

        switch (_output3Mode) {
            case 1:
                GateOut(2, gateHigh);
                break;
            case 2:
            case 3:
            case 4:
            case 5:
                OutputEnvelope(2, gateHigh, _output3Mode);
                break;
            default:
                Out(2, 0);
                break;
        }

        switch (_output4Mode) {
            case 1:
                GateOut(3, gateHigh);
                break;
            case 2:
            case 3:
            case 4:
            case 5:
                OutputEnvelope(3, gateHigh, _output4Mode);
                break;
            default:
                Out(3, 0);
                break;
        }
    }

    void View() {
        DrawHeader();
        DrawInterface();
    }

    /////////////////////////////////////////////////////////////////
    // Control handlers
    /////////////////////////////////////////////////////////////////
    void OnLeftButtonPress() {
    }

    void OnLeftButtonLongPress() {
    }

    void OnRightButtonPress() {
        switch (_activeMenuItems[_cvSource][_selectedMenuIndex]) {
            case Setting::GateProb:
                _gateProbLocked = !_gateProbLocked;
                break;
            case Setting::CVProb:
                _cvProbLocked = !_cvProbLocked;
            default:
                break;
        }
    }

    void OnUpButtonPress() {
        _octave = constrain(_octave + 1, -3, 3);
    }

    void OnDownButtonPress() {
        _octave = constrain(_octave - 1, -3, 3);
    }

    void OnDownButtonLongPress() {
    }

    void OnLeftEncoderMove(int delta) {
        int maxIndex = _menuSize[_cvSource] - 1;
        _selectedMenuIndex = constrain(_selectedMenuIndex + delta, 0, maxIndex);

        if (delta > 0 && (_selectedMenuIndex > (_topMenuIndex + (MELODY_PAGESIZE - 2)))) {
            _topMenuIndex = constrain(_selectedMenuIndex - (MELODY_PAGESIZE - 2), 0, maxIndex - (MELODY_PAGESIZE - 1));
        } else if (delta < 0 && _selectedMenuIndex <= _topMenuIndex && _topMenuIndex > 0) {
            _topMenuIndex = _selectedMenuIndex > 0 ? _selectedMenuIndex - 1 : 0;
        }
    }

    void OnRightEncoderMove(int direction) {
        switch (_activeMenuItems[_cvSource][_selectedMenuIndex]) {
            case Setting::Root:
                _root = constrain(_root + direction, 0, 11);
                break;
            case Setting::Scale:
                _scale += direction;
                if (_scale >= MELODY_MAX_SCALE) {
                    _scale = 0;
                } else if (_scale < 0) {
                    _scale = MELODY_MAX_SCALE - 1;
                }
                _quantizer.Configure(OC::Scales::GetScale(_scale), 0xffff);
                break;
            case Setting::GateLength:
                _gateRegisterLength = constrain(_gateRegisterLength += direction, 2, 16);
                break;
            case Setting::GateProb:
                _gateProb = constrain(_gateProb += direction, 0, 100);
                break;
            case Setting::GateUnlockSource:
                _gateUnlockSource = constrain(_gateUnlockSource += direction, 0, 4);
                break;
            case Setting::GateProbModSource:
                _gateProbModSource = constrain(_gateProbModSource += direction, 0, 3);
                break;
            case Setting::CVLength:
                _cvRegisterLength = constrain(_cvRegisterLength += direction, 2, 16);
                break;
            case Setting::CVProb:
                _cvProb = constrain(_cvProb += direction, 0, 100);
                break;
            case Setting::CVUnlockSource:
                _cvUnlockSource = constrain(_cvUnlockSource += direction, 0, 4);
                break;
            case Setting::CVProbModSource:
                _cvProbModSource = constrain(_cvProbModSource += direction, 0, 3);
                break;
            case Setting::CVRange:
                _cvRange = constrain(_cvRange += direction, 1, 24);
                break;
            case Setting::CVRangeModSource:
                _cvRangeModSource = constrain(_cvRangeModSource += direction, 0, 3);
                break;
            case Setting::Octave:
                _octave = constrain(_octave += direction, -3, 3);
                break;
            case Setting::BassOctave:
                _bassOctave = constrain(_bassOctave += direction, -3, 3);
                break;
            case Setting::OctaveShiftMode:
                _octaveShiftUp = !_octaveShiftUp;
                break;
            case Setting::OctaveShiftSource:
                _octaveShiftSource = constrain(_octaveShiftSource += direction, 0, 2);
                break;
            case Setting::Output3Mode:
                _output3Mode = constrain(_output3Mode += direction, 0, 5);
                break;
            case Setting::Output4Mode:
                _output4Mode = constrain(_output4Mode += direction, 0, 5);
                break;
            case Setting::CVSource:
                _cvSource = constrain(_cvSource += direction, 0, (int)ARRAY_SIZE(_cvSources) - 1);
                break;
            case Setting::ArpDirection:
                _arpDirection = constrain(_arpDirection += direction, 0, (int)ARRAY_SIZE(_arpDirections) - 1);
                break;
            case Setting::Chord:
                _chord += direction;
                if (_chord >= (int)ARRAY_SIZE(Arp_Chords)) {
                    _chord = 0;
                } else if (_chord < 0) {
                    _chord = (int)ARRAY_SIZE(Arp_Chords) - 1;
                }
                break;
            case Setting::ArpMode:
                _arpMode = constrain(_arpMode += direction, 0, 1);
                break;
        }
    }

   private:
    braids::Quantizer _quantizer;
    EnvelopeGenerator _envelopeGenerator;

    const char *const _noteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int _currentNote;

    // 16-bit LFSR registers
    uint16_t _cvRegister;
    uint16_t _gateRegister;

    // settings
    uint8_t _root;
    uint8_t _scale;
    uint8_t _gateRegisterLength;
    int8_t _gateProb;
    int8_t _gateUnlockSource;
    int8_t _gateProbModSource;
    uint8_t _cvRegisterLength;
    int8_t _cvProb;
    int8_t _cvUnlockSource;
    int8_t _cvProbModSource;
    uint8_t _cvRange;
    int8_t _cvRangeModSource;
    int8_t _octave;
    bool _octaveShiftUp;
    int8_t _octaveShiftSource;
    bool _gateProbLocked;
    bool _cvProbLocked;
    int8_t _output3Mode;
    int8_t _output4Mode;
    int8_t _bassOctave;
    int8_t _cvSource;
    int8_t _arpDirection;

    // setting overrides
    uint8_t _moddedCvRange;
    uint8_t _moddedCVProb;
    uint8_t _moddedGateProb;

    // state variables
    int32_t _currentMelodyNote;
    bool _lastGateHigh;
    int8_t _arpNote;
    int8_t _chord;
    int8_t _arpMode;

    // -------------- //
    // menu structure //
    // -------------- //

    // All possible settings
    enum Setting {
        Root = 0,
        Scale,
        GateLength,
        CVLength,
        GateProb,
        CVProb,
        CVRange,
        Octave,
        GateUnlockSource,
        GateProbModSource,
        CVUnlockSource,
        CVProbModSource,
        OctaveShiftMode,
        OctaveShiftSource,
        CVRangeModSource,
        Output3Mode,
        Output4Mode,
        BassOctave,
        CVSource,
        ArpDirection,
        Chord,
        ArpMode
    };

    // Menu items when source == LFSR
    Setting _lfsrMenuItems[19] = {
        CVSource,
        Root,
        Scale,
        GateLength,
        CVLength,
        GateProb,
        CVProb,
        CVRange,
        Octave,
        BassOctave,
        GateUnlockSource,
        GateProbModSource,
        CVUnlockSource,
        CVProbModSource,
        CVRangeModSource,
        OctaveShiftMode,
        OctaveShiftSource,
        Output3Mode,
        Output4Mode};

    // Menu items when source == Arpeggio
    Setting _arpMenuItems[17] = {
        CVSource,
        ArpDirection,
        ArpMode,
        Root,
        Chord,
        GateLength,
        GateProb,
        Octave,
        BassOctave,
        GateUnlockSource,
        GateProbModSource,
        CVUnlockSource,
        CVProbModSource,
        OctaveShiftMode,
        OctaveShiftSource,
        Output3Mode,
        Output4Mode};
    // Annoyingly need to store the sizes of the 2 menu arrays
    // there must be a better way than this, but can't find it
    uint8_t _menuSize[2]{19, 17};

    uint8_t _selectedMenuIndex;
    uint8_t _topMenuIndex;

    Setting *_activeMenuItems[2] = {_lfsrMenuItems, _arpMenuItems};

    // --------------

    // envelopes ({attack, decay, sustain, release})
    // Attack rate from 1-255 where 1 is fast
    // Decay rate from 1-255 where 1 is fast
    // Sustain level from 1-255 where 1 is low
    // Release rate from 1-255 where 1 is fast
    const int _envelopes[4][4] = {{2, 15, 150, 25}, {2, 15, 150, 8}, {20, 15, 150, 8}, {20, 15, 150, 25}};

    // Stage management
    int _envelopeStage[4];           // The current ASDR stage of the current envelope
    int _envelopeStageTicks[4];      // Current number of ticks into the current stage
    bool _envelopeGated[4];          // Gate was on in last tick
    simfloat _envelopeAmplitude[4];  // Amplitude of the envelope at the current position

    const char *const _digitalInputNames[5] = {
        "None", "Dig1", "Dig2", "Dig3", "Dig4"};
    const char *const _cvInputNames[5] = {
        "None", "CV1", "CV2", "CV3", "CV4"};
    const char *const _outputModeNames[6] = {
        "None", "Gate", "Env 1", "Env 2", "Env 3", "Env 4"};
    const char *const _cvSources[2] = {
        "LFSR", "Arpeggio"};
    const char *const _arpDirections[3] = {
        "Up", "Down", "Random"};
    const char *const _arpModes[2] = {
        "Note", "Clocked"};

    void DrawInterface() {
        const uint8_t nameWidth = 70;

        for (uint8_t i = 0; i < MELODY_PAGESIZE; i++) {
            uint8_t index = _topMenuIndex + i;

            if (index > _menuSize[_cvSource] - 1) {
                break;
            }

            Setting item = _activeMenuItems[_cvSource][_topMenuIndex + i];
            uint8_t y = 16 + (10 * i);

            // print the setting name
            gfxPrint(0, y, GetSettingName(item));
            if (index == _selectedMenuIndex) {
                gfxInvert(0, y - 1, nameWidth, 10);
            }

            // print the value
            const uint8_t x = 74;
            switch (item) {
                case Setting::Root:
                    gfxPrint(x, y, OC::Strings::note_names_unpadded[_root]);
                    break;
                case Setting::Scale:
                    gfxPrint(x, y, OC::scale_names_short[_scale]);
                    break;
                case Setting::GateLength:
                    gfxPrint(x, y, _gateRegisterLength);
                    break;
                case Setting::GateProb:
                    if (_gateProbLocked && !GateProbUnlockedByInput()) {
                        gfxBitmap(x, y - 1, 8, LOCK_ICON);
                    } else {
                        gfxPrint(x, y, _gateProb);
                        if (_gateProbModSource > 0 && DetentedIn(_gateProbModSource - 1)) {
                            gfxPrint(x + 18, y, "(");
                            gfxPrint(_moddedGateProb);
                            gfxPrint(")");
                        }
                    }
                    break;
                case Setting::GateUnlockSource:
                    gfxPrint(x, y, _digitalInputNames[_gateUnlockSource]);
                    break;
                case Setting::GateProbModSource:
                    gfxPrint(x, y, _cvInputNames[_gateProbModSource]);
                    break;
                case Setting::CVLength:
                    gfxPrint(x, y, _cvRegisterLength);
                    break;
                case Setting::CVProb:
                    if (_cvProbLocked && !CVProbUnlockedByInput()) {
                        gfxBitmap(x, y - 1, 8, LOCK_ICON);
                    } else {
                        gfxPrint(x, y, _cvProb);
                        if (_cvProbModSource > 0 && DetentedIn(_cvProbModSource - 1)) {
                            gfxPrint(x + 18, y, "(");
                            gfxPrint(_moddedCVProb);
                            gfxPrint(")");
                        }
                    }
                    break;
                case Setting::CVUnlockSource:
                    gfxPrint(x, y, _digitalInputNames[_cvUnlockSource]);
                    break;
                case Setting::CVProbModSource:
                    gfxPrint(x, y, _cvInputNames[_cvProbModSource]);
                    break;
                case Setting::CVRange:
                    gfxPrint(x, y, _cvRange);
                    if (_cvRangeModSource > 0 && DetentedIn(_cvRangeModSource - 1)) {
                        gfxPrint(x + 18, y, "(");
                        gfxPrint(_moddedCvRange);
                        gfxPrint(")");
                    }
                    break;
                case Setting::CVRangeModSource:
                    gfxPrint(x, y, _cvInputNames[_cvRangeModSource]);
                    break;
                case Setting::Octave:
                    gfxPrint(x, y, _octave);
                    break;
                case Setting::OctaveShiftMode:
                    gfxPrint(x, y, _octaveShiftUp ? "+" : "-");
                    break;
                case Setting::OctaveShiftSource:
                    gfxPrint(x, y, _digitalInputNames[_octaveShiftSource]);
                    break;
                case Setting::Output3Mode:
                    gfxPrint(x, y, _outputModeNames[_output3Mode]);
                    break;
                case Setting::Output4Mode:
                    gfxPrint(x, y, _outputModeNames[_output4Mode]);
                    break;
                case Setting::BassOctave:
                    gfxPrint(x, y, _bassOctave);
                    break;
                case Setting::CVSource:
                    gfxPrint(x, y, _cvSources[_cvSource]);
                    break;
                case Setting::ArpDirection:
                    gfxPrint(x, y, _arpDirections[_arpDirection]);
                    break;
                case Setting::Chord:
                    gfxPrint(x, y, Arp_Chords[_chord].chord_name);
                    break;
                case Setting::ArpMode:
                    gfxPrint(x, y, _arpModes[_arpMode]);
                    break;
                default:
                    break;
            }
        }
    }

    void DrawHeader() {
        gfxPrint(1, 2, "Melody Maker");

        int positiveModulusNoteValue = (_currentNote % 12 + 12) % 12;
        gfxPrint(115, 2, OC::Strings::note_names[positiveModulusNoteValue]);

        // Indicators for octave shifting
        if (_octaveShiftSource > 0) {
            if (_octaveShiftUp && Gate(_octaveShiftSource)) {
                gfxPrint(93, 2, "+");
            } else if (!_octaveShiftUp && Gate(_octaveShiftSource)) {
                gfxPrint(93, 2, "-");
            }
        }

        gfxLine(0, 10, 127, 10);
        gfxLine(0, 13, 127, 13);

        // draw a representation of the gate register
        const int8_t blockWidth = 8;
        const int8_t blockHeight = 3;

        for (int b = 0; b < 16; b++) {
            int v = (_gateRegister >> b) & 0x01;
            if (!v) {
                gfxRect(blockWidth * b, 10, blockWidth, blockHeight);
            }
        }
    }

    const char *GetSettingName(Setting setting) {
        switch (setting) {
            case Setting::Root:
                return "Root";
            case Setting::Scale:
                return "Scale";
            case Setting::GateLength:
                return "Gt Reg Len";
            case Setting::CVLength:
                return "CV Reg Len";
            case Setting::GateProb:
                return "Gate Prob";
            case Setting::CVProb:
                return "CV Prob";
            case Setting::CVRange:
                return "CV Range";
            case Setting::Octave:
                return "Octave";
            case Setting::GateUnlockSource:
                return "Gt Unlock";
            case Setting::GateProbModSource:
                return "Gt Prb Mod";
            case Setting::CVUnlockSource:
                return "CV Unlock";
            case Setting::CVProbModSource:
                return "CV Prb Mod";
            case Setting::CVRangeModSource:
                return "CV Rng Mod";
            case Setting::OctaveShiftMode:
                return "Oct Mode";
            case Setting::OctaveShiftSource:
                return "Oct Gate In";
            case Setting::Output3Mode:
                return "Out C";
            case Setting::Output4Mode:
                return "Out D";
            case Setting::BassOctave:
                return "Bass Octave";
            case Setting::CVSource:
                return "CV Source";
            case Setting::ArpDirection:
                return "Arp Dir";
            case Setting::Chord:
                return "Chord";
            case Setting::ArpMode:
                return "Arp Mode";
            default:
                return "";
        }
    }

    void AdvanceCVRegister(int prob) {
        // Grab the bit that's about to be shifted away
        int cvLast = (_cvRegister >> (_cvRegisterLength - 1)) & 0x01;

        // Does it change?
        if (random(0, 99) < prob) {
            cvLast = 1 - cvLast;
        }

        // Shift left, then potentially add the bit from the other side
        _cvRegister = (_cvRegister << 1) + cvLast;
    }

    void AdvanceGateRegister(int prob) {
        // Grab the bit that's about to be shifted away
        int gateLast = (_gateRegister >> (_gateRegisterLength - 1)) & 0x01;

        // Does it change?
        if (random(0, 99) < prob) {
            // XOR a random value with the high bit to get a new value
            bool data = random(0, HEMISPHERE_3V_CV * 2) > HEMISPHERE_3V_CV ? 0x01 : 0x00;
            gateLast = (data != gateLast);
        }

        // Shift left, then potentially add the bit from the other side
        _gateRegister = (_gateRegister << 1) + gateLast;
    }

    bool GateProbUnlockedByInput() {
        if (_gateUnlockSource > 0) {
            return Gate(_gateUnlockSource - 1);
        } else {
            return false;
        }
    }

    bool CVProbUnlockedByInput() {
        if (_cvUnlockSource > 0) {
            return Gate(_cvUnlockSource - 1);
        } else {
            return false;
        }
    }

    void OutputEnvelope(int ch, bool gateHigh, int outputMode) {
        int attack = _envelopes[outputMode - 2][0];
        int decay = _envelopes[outputMode - 2][2];
        int sustain = _envelopes[outputMode - 2][3];
        int release = _envelopes[outputMode - 2][4];
        Out(ch, _envelopeGenerator.GetEnvelopeAmplitude(gateHigh, _envelopeGated[ch], attack, decay, sustain, release, _envelopeStage[ch], _envelopeStageTicks[ch], _envelopeAmplitude[ch]));
    }

    int32_t GetNote(uint16_t lfsr, uint8_t lfsrLength, uint8_t range, int8_t octave, bool updateCurrentNote = false) {
        const int modulus = 12;

        int note = (lfsr & 0xff) * range;
        note = (note >> (lfsrLength > 7 ? 8 : lfsrLength)) % modulus;

        if (updateCurrentNote) {
            _currentNote = range / 2 - note;
        }

        //76 == C3
        //64 == C2 ("low C")
        //52 == C1
        int32_t quantized = _quantizer.Lookup(64 + range / 2 - note) + (_root << 7);

        // Transpose based on Octave setting
        int32_t octaveShift = octave * (12 << 7);

        // Shift octave up or down controled by a gate to [_octaveShiftSource]
        if (_octaveShiftSource > 0) {
            octaveShift = octaveShift + (_octaveShiftUp ? (Gate(_octaveShiftSource) * (12 << 7)) : (Gate(_octaveShiftSource) ? -1 * (12 << 7) : 0));
        }

        return quantized + octaveShift;
    }

    int32_t GetArpNote(int8_t octave, bool noOctaveTranspose = false, bool updateCurrentNote = false) {
        hem_arp_chord chord = Arp_Chords[_chord];

        switch (_arpDirection) {
            case 0:  //up
                _arpNote++;
                if (_arpNote > (int8_t)chord.nr_notes - 1) {
                    _arpNote = 0;
                }
                break;
            case 1:  //down
                _arpNote--;
                if (_arpNote < 0) {
                    _arpNote = (int8_t)chord.nr_notes - 1;
                }
                break;
            case 2:  //random
                _arpNote = random(0, chord.nr_notes);
            default:
                break;
        }

        if (updateCurrentNote) {
            _currentNote = (chord.chord_tones[_arpNote] % 24);
        }

        // % 24 == restrict to 2 octaves
        int32_t quantized = _quantizer.Lookup(64 + (chord.chord_tones[_arpNote] % 24)) + (_root << 7);

        // Transpose based on Octave setting
        int32_t octaveShift = octave * (12 << 7);

        // Shift octave up or down controled by a gate to [_octaveShiftSource]
        if (!noOctaveTranspose && _octaveShiftSource > 0) {
            octaveShift = octaveShift + (_octaveShiftUp ? (Gate(1) * (12 << 7)) : (Gate(1) ? -1 * (12 << 7) : 0));
        }

        return quantized + octaveShift;
    }
};

MelodyMaker melody_maker_instance;

// App stubs
void MelodyMaker_init() {
    melody_maker_instance.BaseStart();
}

// Not using O_C Storage
size_t MelodyMaker_storageSize() {
    return 0;
}
size_t MelodyMaker_save(void *storage) {
    return 0;
}
size_t MelodyMaker_restore(const void *storage) {
    return 0;
}

void MelodyMaker_isr() {
    return melody_maker_instance.BaseController();
}

void MelodyMaker_handleAppEvent(OC::AppEvent event) {
}

void MelodyMaker_loop() {}

void MelodyMaker_menu() {
    melody_maker_instance.BaseView();
}

void MelodyMaker_screensaver() {}

void MelodyMaker_handleButtonEvent(const UI::Event &event) {
    // For left encoder, handle press and long press
    if (event.control == OC::CONTROL_BUTTON_L) {
        if (event.type == UI::EVENT_BUTTON_LONG_PRESS)
            melody_maker_instance.OnLeftButtonLongPress();
        else
            melody_maker_instance.OnLeftButtonPress();
    }

    // For right encoder, only handle press (long press is reserved)
    if (event.control == OC::CONTROL_BUTTON_R && event.type == UI::EVENT_BUTTON_PRESS)
        melody_maker_instance.OnRightButtonPress();

    // For up button, handle only press (long press is reserved)
    if (event.control == OC::CONTROL_BUTTON_UP)
        melody_maker_instance.OnUpButtonPress();

    // For down button, handle press and long press
    if (event.control == OC::CONTROL_BUTTON_DOWN) {
        if (event.type == UI::EVENT_BUTTON_PRESS)
            melody_maker_instance.OnDownButtonPress();
        if (event.type == UI::EVENT_BUTTON_LONG_PRESS)
            melody_maker_instance.OnDownButtonLongPress();
    }
}

void MelodyMaker_handleEncoderEvent(const UI::Event &event) {
    // Left encoder turned
    if (event.control == OC::CONTROL_ENCODER_L)
        melody_maker_instance.OnLeftEncoderMove(event.value);

    // Right encoder turned
    if (event.control == OC::CONTROL_ENCODER_R)
        melody_maker_instance.OnRightEncoderMove(event.value);
}
