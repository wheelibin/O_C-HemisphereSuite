// Copyright (c) 2019, Jon Wheeler
//
// Based on Braids Quantizer, Copyright 2015 Olivier Gillet.
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

/*
   Random melody maker
*/

#include "OC_scales.h"
#include "braids_quantizer.h"
#include "braids_quantizer_scales.h"
#include "envgen/envelope_generator.h"

#define MEL_MAX_SCALE 63
#define MEL_MIN_LENGTH 2
#define MEL_MAX_LENGTH 16

#define MEL_CURSOR_CV_PROB 0
#define MEL_CURSOR_GATE_PROB 1
#define MEL_CURSOR_CV_LENGTH 2
#define MEL_CURSOR_GATE_LENGTH 3
#define MEL_CURSOR_DIG2_MODE 4
#define MEL_CURSOR_OUT2_MODE 5
#define MEL_CURSOR_OCTAVE 6
#define MEL_CURSOR_RANGE 7
#define MEL_CURSOR_ROOT 8
#define MEL_CURSOR_SCALE 9
#define MEL_CURSOR_MAX 9

class Melody : public HemisphereApplet {
   public:
    const char* applet_name() {
        return "Melody";
    }

    void Start() {
        _cvRegister = random(0, 65535);
        _gateRegister = random(0, 0xffff);
        _cvProb = 0;
        _gateProb = 0;
        _cvRegisterLength = 16;
        _gateRegisterLength = 16;
        _cursor = MEL_CURSOR_CV_PROB;
        _scale = OC::Scales::SCALE_SEMI + 1;  // Ionian/Major
        _root = 0;                            // C
        _output2Mode = 1;
        _envelopeStageTicks = 0;
        _envelopeGated = 0;
        _envelopeStage = HEM_EG_NO_STAGE;
        _digital2Mode = 3;
        _CVUnlocked = false;
        _gateUnlocked = false;
        _octave = 1;
        _cvRange = 12;

        _quantizer.Configure(OC::Scales::GetScale(_scale), 0xffff);
        _quantizer.Init();
    }

    void Controller() {
        // CV 1 bi-polar mod over _cvRange
        _moddedCVRange = constrain(_cvRange + Proportion(DetentedIn(0), HEMISPHERE_MAX_CV, 24), 1, 24);

        if (Clock(0)) {
            StartADCLag(0);
        }

        if (EndOfADCLag(0)) {
            // If the cursor is not on the p value, and Digital 2 is not gated, the sequence remains the same
            int gate_prob = (_gateUnlocked || ((_digital2Mode == 1 || _digital2Mode == 2) && Gate(1))) ? _gateProb : 0;

            AdvanceGateRegister(gate_prob);

            bool gate_high = _gateRegister & 0x01;

            // Get a new note if there is a new high gate
            if (gate_high && (gate_high != _lastGateHigh)) {
                int cvProb = (_CVUnlocked || ((_digital2Mode == 0 || _digital2Mode == 2) && Gate(1))) ? _cvProb : 0;
                AdvanceCVRegister(cvProb);
            }

            int note = (_cvRegister & 0xff) * _moddedCVRange;
            const int modulus = 24;
            note = (note >> (_cvRegisterLength > 7 ? 8 : _cvRegisterLength)) % modulus;

            int32_t quantized = _quantizer.Lookup(64 + _moddedCVRange / 2 - note) + (_root << 7);

            // Transpose based on Octave setting
            int32_t shift_oct = 0;
            if (_octave != 0) {
                shift_oct = _octave * (12 << 7);
            }

            // Transpose up or down based on _digital2Mode
            int32_t shift_alt = 0;
            if (_digital2Mode == 3) shift_alt = Gate(1) * (12 << 7);
            if (_digital2Mode == 4) shift_alt = Gate(1) ? -1 * (12 << 7) : 0;

            Out(0, quantized + shift_oct + shift_alt);

            _lastGateHigh = gate_high;
        }

        bool gate_high = _gateRegister & 0x01;
        if (_output2Mode > 0) {
            OutputEnvelope(gate_high, _output2Mode);
        } else {
            GateOut(1, gate_high);
        }
    }

    void View() {
        // draw a custom header which includes the gate reg display
        DrawHeader();

        // draw the UI
        DrawSelector();
    }

    void OnButtonPress() {
        if (++_cursor > MEL_CURSOR_MAX) _cursor = 0;
        _CVUnlocked = false;
        _gateUnlocked = false;
    }

    void OnEncoderMove(int direction) {
        /*
        c=100  g=100
        L 16   L 16
        R C#   PEN-
        O +3   R 10
        I BOTH O ENV1
      */
        if (_cursor == MEL_CURSOR_CV_PROB) {
            _cvProb = constrain(_cvProb += direction, 0, 100);
            _CVUnlocked = true;
        }
        if (_cursor == MEL_CURSOR_GATE_PROB) {
            _gateProb = constrain(_gateProb += direction, 0, 100);
            _gateUnlocked = true;
        }
        if (_cursor == MEL_CURSOR_CV_LENGTH) _cvRegisterLength = constrain(_cvRegisterLength += direction, MEL_MIN_LENGTH, MEL_MAX_LENGTH);
        if (_cursor == MEL_CURSOR_GATE_LENGTH) _gateRegisterLength = constrain(_gateRegisterLength += direction, MEL_MIN_LENGTH, MEL_MAX_LENGTH);
        if (_cursor == MEL_CURSOR_ROOT) _root = constrain(_root + direction, 0, 11);
        if (_cursor == MEL_CURSOR_SCALE) {
            _scale += direction;
            if (_scale >= MEL_MAX_SCALE) _scale = 0;
            if (_scale < 0) _scale = MEL_MAX_SCALE - 1;
            _quantizer.Configure(OC::Scales::GetScale(_scale), 0xffff);
        }
        if (_cursor == MEL_CURSOR_DIG2_MODE) _digital2Mode = constrain(_digital2Mode += direction, 0, 4);
        if (_cursor == MEL_CURSOR_OUT2_MODE) _output2Mode = constrain(_output2Mode += direction, 0, 4);

        if (_cursor == MEL_CURSOR_OCTAVE) _octave = constrain(_octave += direction, -3, 3);
        if (_cursor == MEL_CURSOR_RANGE) _cvRange = constrain(_cvRange += direction, 1, 24);
    }

    uint32_t OnDataRequest() {
        uint32_t data = 0;
        Pack(data, PackLocation{0, 16}, _cvRegister);
        Pack(data, PackLocation{16, 16}, _gateRegister);
        return data;
    }

    void OnDataReceive(uint32_t data) {
        _cvRegister = Unpack(data, PackLocation{0, 16});
        _gateRegister = Unpack(data, PackLocation{16, 16});
    }

   protected:
    void SetHelp() {
        //                               "------------------" <-- Size Guide
        help[HEMISPHERE_HELP_DIGITALS] = "1=Clock 2=Assigned";
        help[HEMISPHERE_HELP_CVS]      = "1=Rng mod";
        help[HEMISPHERE_HELP_OUTS]     = "A=Pitch B=Assigned";
        help[HEMISPHERE_HELP_ENCODER]  = "See wiki";
        //                               "------------------" <-- Size Guide
    }

   private:
    int _cursor;
    braids::Quantizer _quantizer;
    EnvelopeGenerator _envelopeGenerator;

    // Settings
    uint16_t _cvRegister;     // 16-bit sequence register
    uint16_t _gateRegister;   // Gate register
    int _cvRegisterLength;    // CV Sequence length
    int _gateRegisterLength;  // Gate Sequence length
    int _cvProb;              // Probability of bit 15 changing on each cycle
    int _gateProb;            // Probability of bit 15 changing on each cycle
    int8_t _scale;            // Scale used for quantized output
    int _root;                // Root note for quantizer
    int _output2Mode;         // controls output for Output 2 (0=gate, 1=env1, 2=env2, 3=env3, 4=env4)
    int _digital2Mode;        // 0=unlock cv, 1=unlock gate, 2=unlock both
    int _octave;
    int _cvRange;
    int _moddedCVRange;

    bool _lastGateHigh;  // The last gate value used
    bool _CVUnlocked;
    bool _gateUnlocked;

    // envelopes ({attack, decay, sustain, release})
    // Attack rate from 1-255 where 1 is fast
    // Decay rate from 1-255 where 1 is fast
    // Sustain level from 1-255 where 1 is low
    // Release rate from 1-255 where 1 is fast
    const int _envelopes[4][4] = {{2, 15, 150, 25}, {2, 15, 150, 8}, {20, 15, 150, 8}, {20, 15, 150, 25}};

    // Stage management
    int _envelopeStage;           // The current ASDR stage of the current envelope
    int _envelopeStageTicks;      // Current number of ticks into the current stage
    bool _envelopeGated;          // Gate was on in last tick
    simfloat _envelopeAmplitude;  // Amplitude of the envelope at the current position

    void DrawHeader() {
        gfxPrint(1, 2, applet_name());

        // Indicators for _octave shifting
        if (_digital2Mode == 3 && Gate(1)) {
            gfxPrint(46, 2, "+");
        }
        if (_digital2Mode == 4 && Gate(1)) {
            gfxPrint(46, 2, "-");
        }

        gfxLine(0, 10, 62, 10);
        gfxLine(0, 12, 62, 12);

        // draw a representation of the gate register
        for (int b = 0; b < 16; b++) {
            int v = (_gateRegister >> b) & 0x01;
            if (v) gfxRect(60 - (4 * b), 10, 4, 2);
        }
    }

    void DrawSelector() {
        const uint8_t notes_icon[8] = {0xc0, 0xe0, 0xe0, 0xe0, 0x7f, 0x02, 0x14, 0x08};
        const uint8_t out_icon[8] = {0x18, 0x18, 0x18, 0x18, 0x18, 0x7e, 0x3c, 0x18};
        const uint8_t octave_icon[8] = {0x81, 0x99, 0xa5, 0xc3, 0xc3, 0xa5, 0x99, 0x81};
        const uint8_t range_icon[8] = {0x00, 0x24, 0x66, 0xff, 0x66, 0x24, 0x00, 0x00};

        const char* const digital2Modes[5] = {"CV", "Gt", "C+G", "OC+", "OC-"};
        const char* const output2Modes[5] = {"Gt", "E1", "E2", "E3", "E4"};

        const int linesY[5] = {15, 25, 35, 45, 55};

        // CV prob
        gfxPrint(1, linesY[0], "c=");
        if (_cursor == MEL_CURSOR_CV_PROB) gfxCursor(12, linesY[0] + 8, 12);
        if (_CVUnlocked || ((_digital2Mode == 0 || _digital2Mode == 2) && Gate(1))) {
            gfxPrint(13, linesY[0], _cvProb);
        } else {
            gfxBitmap(13, linesY[0] - 1, 8, LOCK_ICON);
        }

        // Gate prob
        gfxPrint(34, linesY[0], "g=");
        if (_cursor == MEL_CURSOR_GATE_PROB) gfxCursor(46, linesY[0] + 8, 12);
        if (_gateUnlocked || ((_digital2Mode == 1 || _digital2Mode == 2) && Gate(1))) {
            gfxPrint(46, linesY[0], _gateProb);
        } else {
            gfxBitmap(46, linesY[0] - 1, 8, LOCK_ICON);
        }

        // CV register Length
        gfxBitmap(1, linesY[1] - 1, 8, LOOP_ICON);
        gfxPrint(10, linesY[1], _cvRegisterLength);
        if (_cursor == MEL_CURSOR_CV_LENGTH) gfxCursor(10, linesY[1] + 8, 12);

        // Gate register Length
        gfxBitmap(34, linesY[1] - 1, 8, LOOP_ICON);
        gfxPrint(43, linesY[1], _gateRegisterLength);
        if (_cursor == MEL_CURSOR_GATE_LENGTH) gfxCursor(43, linesY[1] + 8, 12);

        // Root
        gfxBitmap(1, linesY[4] - 1, 8, notes_icon);
        gfxPrint(10, linesY[4], OC::Strings::note_names_unpadded[_root]);
        if (_cursor == MEL_CURSOR_ROOT) gfxCursor(10, linesY[4] + 8, 12);

        // Scale
        gfxPrint(27, linesY[4], OC::scale_names_short[_scale]);
        if (_cursor == MEL_CURSOR_SCALE) gfxCursor(27, linesY[4] + 8, 30);

        // Octave
        gfxBitmap(1, linesY[3] - 1, 8, octave_icon);
        gfxPrint(10, linesY[3], _octave);
        if (_cursor == MEL_CURSOR_OCTAVE) gfxCursor(10, linesY[3] + 8, 12);

        // Range
        gfxBitmap(34, linesY[3] - 1, 8, range_icon);
        gfxPrint(43, linesY[3], _moddedCVRange);
        if (_cursor == MEL_CURSOR_RANGE) gfxCursor(43, linesY[3] + 8, 12);

        // Digital 2 mode
        gfxBitmap(1, linesY[2] - 1, 8, MOD_ICON);
        gfxPrint(10, linesY[2], digital2Modes[_digital2Mode]);
        if (_cursor == MEL_CURSOR_DIG2_MODE) gfxCursor(10, linesY[2] + 8, 18);

        // Output 2 Mode
        gfxBitmap(34, linesY[2] - 1, 8, out_icon);
        gfxPrint(43, linesY[2], output2Modes[_output2Mode]);
        if (_cursor == MEL_CURSOR_OUT2_MODE) gfxCursor(43, linesY[2] + 8, 12);
    }

    void AdvanceCVRegister(int prob) {
        // Grab the bit that's about to be shifted away
        int cvLast = (_cvRegister >> (_cvRegisterLength - 1)) & 0x01;

        // Does it change?
        if (random(0, 99) < prob) cvLast = 1 - cvLast;

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

    void OutputEnvelope(bool gateHigh, int outputMode) {
        int attack = _envelopes[outputMode - 2][0];
        int decay = _envelopes[outputMode - 2][2];
        int sustain = _envelopes[outputMode - 2][3];
        int release = _envelopes[outputMode - 2][4];
        Out(1, _envelopeGenerator.GetEnvelopeAmplitude(gateHigh, _envelopeGated, attack, decay, sustain, release, _envelopeStage, _envelopeStageTicks, _envelopeAmplitude));
    }
};

////////////////////////////////////////////////////////////////////////////////
//// Hemisphere Applet Functions
///
///  Once you run the find-and-replace to make these refer to TM,
///  it's usually not necessary to do anything with these functions. You
///  should prefer to handle things in the HemisphereApplet child class
///  above.
////////////////////////////////////////////////////////////////////////////////
Melody Melody_instance[2];

void Melody_Start(bool hemisphere) {
    Melody_instance[hemisphere].BaseStart(hemisphere);
}

void Melody_Controller(bool hemisphere, bool forwarding) {
    Melody_instance[hemisphere].BaseController(forwarding);
}

void Melody_View(bool hemisphere) {
    Melody_instance[hemisphere].BaseView();
}

void Melody_OnButtonPress(bool hemisphere) {
    Melody_instance[hemisphere].OnButtonPress();
}

void Melody_OnEncoderMove(bool hemisphere, int direction) {
    Melody_instance[hemisphere].OnEncoderMove(direction);
}

void Melody_ToggleHelpScreen(bool hemisphere) {
    Melody_instance[hemisphere].HelpScreen();
}

uint32_t Melody_OnDataRequest(bool hemisphere) {
    return Melody_instance[hemisphere].OnDataRequest();
}

void Melody_OnDataReceive(bool hemisphere, uint32_t data) {
    Melody_instance[hemisphere].OnDataReceive(data);
}
