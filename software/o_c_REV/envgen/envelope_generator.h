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

#ifndef int2simfloat
#define int2simfloat(x) (x << 14)
#define simfloat2int(x) (x >> 14)
typedef int32_t simfloat;
#endif

#ifndef ENVELOPE_GENERATOR_H
#define ENVELOPE_GENERATOR_H

// Envelope constants
#define HEM_EG_MAX_VALUE 255
#define HEM_EG_ATTACK 0
#define HEM_EG_DECAY 1
#define HEM_EG_SUSTAIN 2
#define HEM_EG_RELEASE 3
#define HEM_EG_NO_STAGE -1
// About four seconds
#define HEM_EG_MAX_TICKS_AD 33333
// About eight seconds
#define HEM_EG_MAX_TICKS_R 133333

class EnvelopeGenerator {
   public:
    void AttackAmplitude(int attack, int &stage, int &stage_ticks, simfloat &amplitude) {
        int effective_attack = constrain(attack, 1, HEM_EG_MAX_VALUE);
        int total_stage_ticks = Proportion(effective_attack, HEM_EG_MAX_VALUE, HEM_EG_MAX_TICKS_AD);
        int ticks_remaining = total_stage_ticks - stage_ticks;
        if (effective_attack == 1) ticks_remaining = 0;
        if (ticks_remaining <= 0) {  // End of attack; move to decay
            stage = HEM_EG_DECAY;
            stage_ticks = 0;
            amplitude = int2simfloat(HEMISPHERE_MAX_CV);
        } else {
            simfloat amplitude_remaining = int2simfloat(HEMISPHERE_MAX_CV) - amplitude;
            simfloat increase = amplitude_remaining / ticks_remaining;
            amplitude += increase;
        }
    }

    void DecayAmplitude(int decay, int sustain, int &stage, int &stage_ticks, simfloat &amplitude) {
        int total_stage_ticks = Proportion(decay, HEM_EG_MAX_VALUE, HEM_EG_MAX_TICKS_AD);
        int ticks_remaining = total_stage_ticks - stage_ticks;
        simfloat amplitude_remaining = amplitude - int2simfloat(Proportion(sustain, HEM_EG_MAX_VALUE, HEMISPHERE_MAX_CV));
        if (sustain == 1) ticks_remaining = 0;
        if (ticks_remaining <= 0) {  // End of decay; move to sustain
            stage = HEM_EG_SUSTAIN;
            stage_ticks = 0;
            amplitude = int2simfloat(Proportion(sustain, HEM_EG_MAX_VALUE, HEMISPHERE_MAX_CV));
        } else {
            simfloat decrease = amplitude_remaining / ticks_remaining;
            amplitude -= decrease;
        }
    }

    void SustainAmplitude(int sustain, simfloat &amplitude) {
        amplitude = int2simfloat(Proportion(sustain - 1, HEM_EG_MAX_VALUE, HEMISPHERE_MAX_CV));
    }

    void ReleaseAmplitude(int release, int &stage, int &stage_ticks, simfloat &amplitude) {
        int effective_release = constrain(release, 1, HEM_EG_MAX_VALUE) - 1;
        int total_stage_ticks = Proportion(effective_release, HEM_EG_MAX_VALUE, HEM_EG_MAX_TICKS_R);
        int ticks_remaining = total_stage_ticks - stage_ticks;
        if (effective_release == 0) ticks_remaining = 0;
        if (ticks_remaining <= 0 || amplitude <= 0) {  // End of release; turn off envelope
            stage = HEM_EG_NO_STAGE;
            stage_ticks = 0;
            amplitude = 0;
        } else {
            simfloat decrease = amplitude / ticks_remaining;
            amplitude -= decrease;
        }
    }

    int GetEnvelopeAmplitude(bool gateHigh, bool &gated, int &attack, int &decay, int &sustain, int &release, int &stage, int &stage_ticks, simfloat &amplitude) {
        if (gateHigh) {
            if (!gated) {  // The gate wasn't on last time, so this is a newly-gated EG
                stage_ticks = 0;
                if (stage != HEM_EG_RELEASE) amplitude = 0;
                stage = HEM_EG_ATTACK;
                AttackAmplitude(attack, stage, stage_ticks, amplitude);
            } else {  // The gate is STILL on, so process the appopriate stage
                stage_ticks++;
                if (stage == HEM_EG_ATTACK) AttackAmplitude(attack, stage, stage_ticks, amplitude);
                if (stage == HEM_EG_DECAY) DecayAmplitude(decay, sustain, stage, stage_ticks, amplitude);
                if (stage == HEM_EG_SUSTAIN) SustainAmplitude(sustain, amplitude);
            }
            gated = 1;
        } else {
            if (gated) {  // The gate was on last time, so this is a newly-released EG
                stage = HEM_EG_RELEASE;
                stage_ticks = 0;
            }

            if (stage == HEM_EG_RELEASE) {  // Process the release stage, if necessary
                stage_ticks++;
                ReleaseAmplitude(release, stage, stage_ticks, amplitude);
            }
            gated = 0;
        }

        return simfloat2int(amplitude);
    }

   private:
    int Proportion(int numerator, int denominator, int max_value) {
        simfloat proportion = int2simfloat((int32_t)numerator) / (int32_t)denominator;
        int scaled = simfloat2int(proportion * max_value);
        return scaled;
    }
};

#endif  // ENVELOPE_GENERATOR_H
