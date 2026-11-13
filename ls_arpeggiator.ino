/***************************** ls_arpeggiator: LinnStrument Arpeggiator ****************************
Copyright 2023 Roger Linn Design (https://www.rogerlinndesign.com)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
***************************************************************************************************
These routines implement the internal LinnStrument arpeggiator. They closely work together with the
system clock when that is active and with the internal tracking of how notes map for each split to
the actual cell that was pressed, allowing velocity to be continuously varied during the arpeggiator
sequence.
***************************************************************************************************/
     
signed char playingArpNote[NUMSPLITS];                // the last note played by the arpeggiator or -1 if no note is still playing
signed char playingArpChannel[NUMSPLITS];             // the last channel played by the arpeggiator or -1 if no note is still playing
signed char stepArpNote[NUMSPLITS];                   // the current step note of the arpeggiator or -1 if it should be starting from scratch
signed char stepArpChannel[NUMSPLITS];                // the current step channel of the arpeggiator or -1 if it should be starting from scratch
boolean lastArpStepOdd[NUMSPLITS];                    // indicates whether the last arpeggiator step was odd (1-based : 1, 3, 5)
ArpeggiatorDirection arpUpDownState[NUMSPLITS];       // the state in the alternating up/down pattern
signed char arpOctaveState[NUMSPLITS];                // the octave that is used while playing the arpeggiator sequence

unsigned long lastTapTempo = 0;

#define MICROS_PER_MINUTE 60000000UL

void initializeArpeggiator() {
  randomSeed(analogRead(0));

  for (byte split = 0; split < NUMSPLITS; ++split) {
    noteTouchMapping[split].initialize(split);
    resetArpeggiatorState(split);
  }
}

void resetArpeggiatorState(byte split) {
  playingArpNote[split] = -1;
  playingArpChannel[split] = -1;
  stepArpNote[split] = -1;
  stepArpChannel[split] = -1;
  lastArpStepOdd[split] = false;
  arpUpDownState[split] = ArpUp;
  arpOctaveState[split] = 0;
}

byte getArpeggiatorNote(byte split, byte notenum) {
  return getOctaveNote(arpOctaveState[split], notenum);
}

byte getOctaveNote(byte octave, byte notenum) {
  return notenum + (octave * 12);
}

void temporarilyEnableArpeggiator() {
  arpTempoDelta[sensorSplit] = 0;
  midiSendNoteOffForAllTouches(sensorSplit);
  resetArpeggiatorState(sensorSplit);
}

void disableTemporaryArpeggiator() {
  turnArpeggiatorOff(sensorSplit);
}

void handleArpeggiatorNoteOff(byte split, byte notenum, byte channel) {
  // handle replay all differently since it plays multiple notes simultaneously
  if (Global.arpDirection == ArpReplayAll) {
    if (playingArpNote[split] != -1) {
      for (byte octave = 0; octave <= Global.arpOctave; ++octave) {
        midiSendNoteOff(split, getOctaveNote(octave, notenum), channel);
      }
    }
  }
  // if this is a strummed note, always turn all octave notes off
  else if (isStrummedSplit(split)) {
    for (byte octave = 0; octave <= Global.arpOctave; ++octave) {
      midiSendNoteOff(split, getOctaveNote(octave, notenum), channel);
    }
  }
  // handle single note sequences, send the note off and reset the arpeggiator state if the note off was the last played
  else if (playingArpNote[split] == notenum && playingArpChannel[split] == channel) {
    midiSendNoteOff(split, getArpeggiatorNote(split, notenum), channel);
  }

  // reset state when no notes are played at all anymore
  if (noteTouchMapping[split].noteCount == 0) {
    resetArpeggiatorState(split);
  }
}

void turnArpeggiatorOff(byte split) {
  sendArpeggiatorStepMidiOff(split);
  resetArpeggiatorState( split);
}

void sendArpeggiatorStepMidiOff(byte split) {
  if (playingArpNote[split] != -1) {
    if (Global.arpDirection == ArpReplayAll) {
      if (noteTouchMapping[split].noteCount > 0) {
        signed char arpNote = noteTouchMapping[split].firstNote;
        signed char arpChannel = noteTouchMapping[split].firstChannel;

        while (arpNote != -1) {
          for (byte octave = 0; octave <= Global.arpOctave; ++octave) {
            midiSendNoteOff(split, getOctaveNote(octave, arpNote), arpChannel);
          }

          NoteEntry* entry = noteTouchMapping[split].getNoteEntry(arpNote, arpChannel);
          if (entry == NULL) {
            arpNote = -1;
          }
          else {
            arpNote = entry->getNextNote();
            arpChannel = entry->getNextChannel();
          }
        }
      }
    }
    else {
      midiSendNoteOff(split, getArpeggiatorNote(split, playingArpNote[split]), playingArpChannel[split]);
    }
  }
}

inline void checkAdvanceArpeggiator() {
  if (!transportArpRunning) return;

  checkAdvanceArpeggiatorForSplit(LEFT);
  checkAdvanceArpeggiatorForSplit(RIGHT);
}

// Clock pulses per arpeggiator step, indexed by ArpeggiatorStepTempo.
// Special values identify swing rates.
#define TEMPO_ARP_HALF_SWING 0xfe
#define TEMPO_ARP_WHOLE_SWING 0xfd
#define TEMPO_ARP_FOURTH_SWING 0xfc
#define TEMPO_ARP_EIGHTH_SWING 0xfb
#define TEMPO_ARP_THIRTYSECOND_SWING 0xfa

const byte arpTempoChoices[19] = {
  24, // ArpFourth
  12, // ArpEighth
   8, // ArpEighthTriplet
   6, // ArpSixteenth
  TEMPO_ARP_SIXTEENTH_SWING, // ArpSixteenthSwing
   4, // ArpSixteenthTriplet
   3, // ArpThirtysecond
   2, // ArpThirtysecondTriplet
   1, // ArpSixtyfourthTriplet
  96, // ArpWhole
  64, // ArpWholeTriplet
  48, // ArpHalf
  32, // ArpHalfTriplet
  16, // ArpFourthTriplet
  TEMPO_ARP_HALF_SWING, // ArpHalfSwing
  TEMPO_ARP_WHOLE_SWING, // ArpWholeSwing
  TEMPO_ARP_FOURTH_SWING, // ArpFourthSwing
  TEMPO_ARP_EIGHTH_SWING, // ArpEighthSwing
  TEMPO_ARP_THIRTYSECOND_SWING // ArpThirtysecondSwing
};

// Ordered slowest to fastest for Low Row speed adjustment.
// NRPN 236: actual subdivisions slowest-to-fastest, then swing divisions.
// Low Row uses each section as its own speed ladder.
const byte arpTempoOrder[19] = {
  ArpWhole, ArpWholeTriplet,
  ArpHalf, ArpHalfTriplet,
  ArpFourth, ArpFourthTriplet,
  ArpEighth, ArpEighthTriplet,
  ArpSixteenth, ArpSixteenthTriplet,
  ArpThirtysecond, ArpThirtysecondTriplet,
  ArpSixtyfourthTriplet,
  ArpWholeSwing, ArpHalfSwing, ArpFourthSwing,
  ArpEighthSwing, ArpSixteenthSwing, ArpThirtysecondSwing
};

inline void checkAdvanceArpeggiatorForSplit(byte split) {

  if (!isArpeggiatorEnabled(split)) {
    return;
  }

  // Swing changes spacing, not average speed: keep it in its own lane.
  // The regular lane includes straight/triplet plus legacy 1/64 triplet.
  const bool swing = isArpeggiatorTempoSwing();
  const int first = swing ? 13 : 0;
  const int last = swing ? 18 : 12;
  int rateIndex = first;
  for (int i = first; i <= last; ++i) {
    if (arpTempoOrder[i] == Global.arpTempo) {
      rateIndex = i;
      break;
    }
  }

  // A one-column Low Row slide selects the next musical speed, while
  // preserving swing if the starting rate was swung.
  const int combinedIndex = constrain(rateIndex + arpTempoDelta[split], first, last);
  const byte tempo = arpTempoChoices[arpTempoOrder[combinedIndex]];

  boolean advance = false;

  // Preserve the 7:5 swing ratio, except 1/32 swing rounds to 4:2 at 24 PPQ.
  unsigned int swingPeriod = 0;
  unsigned int swingOffset = 0;
  switch (tempo) {
    case TEMPO_ARP_WHOLE_SWING: swingPeriod = 192; swingOffset = 112; break;
    case TEMPO_ARP_HALF_SWING: swingPeriod = 96; swingOffset = 56; break;
    case TEMPO_ARP_FOURTH_SWING: swingPeriod = 48; swingOffset = 28; break;
    case TEMPO_ARP_EIGHTH_SWING: swingPeriod = 24; swingOffset = 14; break;
    case TEMPO_ARP_SIXTEENTH_SWING: swingPeriod = 12; swingOffset = 7; break;
    case TEMPO_ARP_THIRTYSECOND_SWING: swingPeriod = 6; swingOffset = 4; break;
  }
  if (swingPeriod) {
    unsigned int phase = arpClockPhase % swingPeriod;
    advance = phase == 0 || phase == swingOffset;
  }
  else {
    advance = (arpClockPhase % tempo == 0);
  }

  if (advance) {
    advanceArpeggiatorForSplit(split);
  }
}

void advanceArpeggiatorForSplit(byte split) {
  signed char arpNote = -1;
  signed char arpChannel = -1;

  sendArpeggiatorStepMidiOff(split);

  // handle replayAll differently since it plays multiple notes simultaneously
  if (Global.arpDirection == ArpReplayAll) {
    if (noteTouchMapping[split].noteCount > 0) {

      // send all the note ons
      arpNote = noteTouchMapping[split].firstNote;
      arpChannel = noteTouchMapping[split].firstChannel;

      while (arpNote != -1) {

        NoteEntry* entry = noteTouchMapping[split].getNoteEntry(arpNote, arpChannel);
        if (entry == NULL) {
          arpNote = -1;
        }
        else {
          for (byte octave = 0; octave <= Global.arpOctave; ++octave) {
            // after the initial velocity, new velocity values are continuously being calculated simply based
            // on the Z data so that velocity can change during the arpeggiation
            TouchInfo* entry_cell = &cell(entry->getCol(), entry->getRow());
            if (entry_cell->touched == touchedCell) {
              entry_cell->velocity = calcPreferredVelocity(entry_cell->velocityZ);      
            }
            midiSendNoteOn(split, getOctaveNote(octave, arpNote), entry_cell->velocity, arpChannel);
          }

          arpNote = entry->getNextNote();
          arpChannel = entry->getNextChannel();
        }
      }

      playingArpNote[split] = noteTouchMapping[split].firstNote;
      playingArpChannel[split] = noteTouchMapping[split].firstChannel;
      stepArpNote[split] = playingArpNote[split];
      stepArpChannel[split] = playingArpChannel[split];
      lastArpStepOdd[split] = !lastArpStepOdd[split];
    }
    else {
      playingArpNote[split] = -1;
      playingArpChannel[split] = -1;
      stepArpNote[split] = -1;
      stepArpChannel[split] = -1;
    }
  }
  // handle single note sequences
  else {
    if (noteTouchMapping[split].noteCount > 0) {

      switch (Global.arpDirection) {

        // sequence steps upwards
        case ArpUp:
        {
          if (stepArpNote[split] == -1) {
            arpNote = noteTouchMapping[split].firstNote;
            arpChannel = noteTouchMapping[split].firstChannel;
          }
          else {
            NoteEntry* stepEntry = noteTouchMapping[split].getNoteEntry(stepArpNote[split], stepArpChannel[split]);
            if (stepEntry == NULL) {
              arpNote = -1;
            }
            else {
              arpNote = stepEntry->getNextNote();
              arpChannel = stepEntry->getNextChannel();
            }

            // start again from the beginning
            if (arpNote == -1) {
              arpNote = noteTouchMapping[split].firstNote;
              arpChannel = noteTouchMapping[split].firstChannel;
              if (++arpOctaveState[split] > Global.arpOctave) {
                arpOctaveState[split] = 0;
              }
            }
          }
          break;
        }

        // sequence steps downwards
        case ArpDown:
        {
          if (stepArpNote[split] == -1) {
            arpNote = noteTouchMapping[split].lastNote;
            arpChannel = noteTouchMapping[split].lastChannel;
          }
          else {
            NoteEntry* stepEntry = noteTouchMapping[split].getNoteEntry(stepArpNote[split], stepArpChannel[split]);
            if (stepEntry == NULL) {
              arpNote = -1;
            }
            else {
              arpNote = stepEntry->getPreviousNote();
              arpChannel = stepEntry->getPreviousChannel();
            }

            // start again from the end
            if (arpNote == -1) {
              arpNote = noteTouchMapping[split].lastNote;
              arpChannel = noteTouchMapping[split].lastChannel;
              if (++arpOctaveState[split] > Global.arpOctave) {
                arpOctaveState[split] = 0;
              }
            }
          }
          break;
        }

        // sequence steps alternativing upwards and downwards
        case ArpUpDown:
        {
          if (stepArpNote[split] == -1) {
            arpUpDownState[split] = ArpUp;
            arpNote = noteTouchMapping[split].firstNote;
            arpChannel = noteTouchMapping[split].firstChannel;
          }
          else {
            NoteEntry* stepEntry = noteTouchMapping[split].getNoteEntry(stepArpNote[split], stepArpChannel[split]);
            if (stepEntry == NULL) {
              arpNote = -1;
            }
            else {
              if (arpUpDownState[split] == ArpDown) {
                arpNote = stepEntry->getPreviousNote();
                arpChannel = stepEntry->getPreviousChannel();
              }
              else {
                arpNote = stepEntry->getNextNote();
                arpChannel = stepEntry->getNextChannel();
              }
            }

            // change directions
            if (arpNote == -1) {

              // handle the state where there's only one note active
              if (noteTouchMapping[split].firstNote == noteTouchMapping[split].lastNote &&
                  noteTouchMapping[split].firstChannel == noteTouchMapping[split].lastChannel) {

                arpNote = noteTouchMapping[split].firstNote;
                arpChannel = noteTouchMapping[split].firstChannel;

                if (Global.arpOctave != 0) {
                  if (arpUpDownState[split] == ArpUp) {
                    if (arpOctaveState[split] != Global.arpOctave) {
                      arpOctaveState[split]++;
                    }
                    else {
                      arpOctaveState[split]--;
                      arpUpDownState[split] = ArpDown;
                    }
                  }
                  else if (arpUpDownState[split] == ArpDown) {
                    if (arpOctaveState[split] != 0) {
                      arpOctaveState[split]--;
                    }
                    else {
                      arpOctaveState[split]++;
                      arpUpDownState[split] = ArpUp;
                    }
                  }
                }
              }
              // when there's no octave switching, wrap around without repeated notes
              // otherwise continue on the next octave if needed this follows the active direction and wrap around when reaching a boundary
              else {

                if (arpUpDownState[split] == ArpUp) {
                  if (arpOctaveState[split] != Global.arpOctave) {
                    arpNote = noteTouchMapping[split].firstNote;
                    arpChannel = noteTouchMapping[split].firstChannel;
                    arpOctaveState[split]++;
                  }
                  else {
                    arpUpDownState[split] = ArpDown;
                    
                    NoteEntry* entry = noteTouchMapping[split].getNoteEntry(noteTouchMapping[split].lastNote, noteTouchMapping[split].lastChannel);
                    if (entry == NULL) {
                      arpNote = -1;
                    }
                    else if (stepEntry == NULL || !stepEntry->hasTouch()) {
                      arpNote = noteTouchMapping[split].lastNote;
                      arpChannel = noteTouchMapping[split].lastChannel;
                    }
                    else {
                      arpNote = entry->getPreviousNote();
                      arpChannel = entry->getPreviousChannel();
                    }
                  }
                }
                else if (arpUpDownState[split] == ArpDown) {
                  if (arpOctaveState[split] != 0) {
                    arpNote = noteTouchMapping[split].lastNote;
                    arpChannel = noteTouchMapping[split].lastChannel;
                    arpOctaveState[split]--;
                  }
                  else {
                    arpUpDownState[split] = ArpUp;

                    NoteEntry* entry = noteTouchMapping[split].getNoteEntry(noteTouchMapping[split].firstNote, noteTouchMapping[split].firstChannel);
                    if (entry == NULL) {
                      arpNote = -1;
                    }
                    else if (stepEntry == NULL || !stepEntry->hasTouch()) {
                      arpNote = noteTouchMapping[split].firstNote;
                      arpChannel = noteTouchMapping[split].firstChannel;
                    }
                    else {
                      arpNote = entry->getNextNote();
                      arpChannel = entry->getNextChannel();
                    }
                  }
                }
              }
            }
          }
          break;
        }

        // sequence steps randomly
        case ArpRandom:
        {
          long pos = random(noteTouchMapping[split].noteCount);

          arpNote = noteTouchMapping[split].firstNote;
          arpChannel = noteTouchMapping[split].firstChannel;

          while (arpNote != -1 && pos-- != 0) {
            NoteEntry* entry = noteTouchMapping[split].getNoteEntry(arpNote, arpChannel);
            if (entry == NULL) {
              arpNote = -1;
            }
            else {
              arpNote = entry->getNextNote();
              arpChannel = entry->getNextChannel();
            }
          }

          if (Global.arpOctave) {
            arpOctaveState[split] = random(Global.arpOctave + 1);
          }
          break;
        }

        case ArpReplayAll:
          // handled above, at the beginning of the function
          break;
      }

      if (arpNote != -1) {
        // if this is the first step in a new sequence, this will be an odd step (starting at one)
        if (stepArpNote[split] == -1) {
          lastArpStepOdd[split] = true;
        }
        else {
          lastArpStepOdd[split] = !lastArpStepOdd[split];
        }

        // send the MIDI note
        NoteEntry* entry = noteTouchMapping[split].getNoteEntry(arpNote, arpChannel);
        if (entry == NULL) {
          arpNote = -1;
          arpChannel = -1;
        }
        else {
          // after the initial velocity, new velocity values are continuously being calculated simply based
          // on the Z data so that velocity can change during the arpeggiation
          TouchInfo* entry_cell = &cell(entry->getCol(), entry->getRow());
          if (entry_cell->touched == touchedCell) {
            entry_cell->velocity = calcPreferredVelocity(entry_cell->velocityZ);      
          }
          midiSendNoteOn(split, getArpeggiatorNote(split, arpNote), entry_cell->velocity, arpChannel);
        }
      }

      playingArpNote[split] = arpNote;
      playingArpChannel[split] = arpChannel;
      stepArpNote[split] = arpNote;
      stepArpChannel[split] = arpChannel;
    }
  }
}

inline boolean isArpeggiatorEnabled(byte split) {
  return Split[split].arpeggiator || isLowRowArpeggiatorPressed(split);
}
