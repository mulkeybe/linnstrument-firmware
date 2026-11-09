/*
Copyright 2026 Roger Linn Design (https://www.rogerlinndesign.com)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
// The arpeggiator steps through the held notes.

static std::vector<std::vector<uint8_t>> arpNotes() {
  linn::drainMidi();
  std::vector<uint8_t> out = host::serialOutput();
  host::clearSerialOutput();
  std::vector<std::vector<uint8_t>> notes;
  for (size_t i = 0; i + 2 < out.size(); ++i) {
    if ((out[i] & 0xF0) == 0x90 || (out[i] & 0xF0) == 0x80) {
      notes.push_back({out[i], out[i + 1], out[i + 2]});
      i += 2;
    }
  }
  return notes;
}

static void holdTwoNotes() {
  linn::reset();
  Global.velocitySensitivity = velocityFixed;
  Global.valueForFixedVelocity = 77;
  Global.rowOffset = 5;
  Split[LEFT].arpeggiator = true;
  Global.arpOctave = 0;
  linn::press(5, 3, 2000, 2000, 400);
  linn::press(7, 3, 2000, 2000, 400);
  arpNotes();
}

TEST(arpeggiator_up_steps_through_the_held_notes) {
  holdTwoNotes();
  Global.arpDirection = ArpUp;
  advanceArpeggiatorForSplit(LEFT);
  std::vector<std::vector<uint8_t>> step1 = arpNotes();
  CHECK_EQ(step1.size(), 1);
  if (step1.size() == 1) {
    CHECK_EQ(step1[0][0], 0x90);
    CHECK_EQ(step1[0][1], 49);
    CHECK_EQ(step1[0][2], 77);
  }
  advanceArpeggiatorForSplit(LEFT);
  std::vector<std::vector<uint8_t>> step2 = arpNotes();
  CHECK_EQ(step2.size(), 2);
  if (step2.size() == 2) {
    CHECK_EQ(step2[0][0], 0x80);
    CHECK_EQ(step2[0][1], 49);
    CHECK_EQ(step2[1][0], 0x90);
    CHECK_EQ(step2[1][1], 51);
  }
  advanceArpeggiatorForSplit(LEFT);
  std::vector<std::vector<uint8_t>> step3 = arpNotes();
  CHECK_EQ(step3.size(), 2);
  if (step3.size() == 2) CHECK_EQ(step3[1][1], 49);
}

TEST(arpeggiator_replay_all_plays_every_held_note_each_step) {
  holdTwoNotes();
  Global.arpDirection = ArpReplayAll;
  advanceArpeggiatorForSplit(LEFT);
  std::vector<std::vector<uint8_t>> step = arpNotes();
  int ons = 0;
  for (size_t i = 0; i < step.size(); ++i) {
    if (step[i][0] == 0x90) ++ons;
  }
  CHECK_EQ(ons, 2);
}

TEST(arpeggiator_octave_adds_the_note_an_octave_up) {
  holdTwoNotes();
  Global.arpDirection = ArpReplayAll;
  Global.arpOctave = 1;
  advanceArpeggiatorForSplit(LEFT);
  std::vector<std::vector<uint8_t>> step = arpNotes();
  bool high = false;
  for (size_t i = 0; i < step.size(); ++i) {
    if (step[i][0] == 0x90 && step[i][1] == 49 + 12) high = true;
  }
  CHECK(high);
}

TEST(arpeggiator_stops_when_the_notes_are_released) {
  holdTwoNotes();
  Global.arpDirection = ArpUp;
  advanceArpeggiatorForSplit(LEFT);
  CHECK_EQ(arpNotes().size(), 1);
  linn::release(5, 3);
  linn::release(7, 3);
  arpNotes();
  advanceArpeggiatorForSplit(LEFT);
  std::vector<std::vector<uint8_t>> step = arpNotes();
  int ons = 0;
  for (size_t i = 0; i < step.size(); ++i) {
    if (step[i][0] == 0x90) ++ons;
  }
  CHECK_EQ(ons, 0);
}

TEST(arpeggiator_slow_rates_follow_clock_divisions) {
  struct RateTest {
    ArpeggiatorStepTempo rate;
    int pulses;
  };

  const RateTest rates[] = {
    {ArpFourth, 24},
    {ArpFourthTriplet, 16},
    {ArpHalf, 48},
    {ArpHalfTriplet, 32},
    {ArpWhole, 96},
    {ArpWholeTriplet, 64}
  };

  for (const auto& rate : rates) {
    holdTwoNotes();
    Global.arpDirection = ArpUp;
    Global.arpTempo = rate.rate;
    arpTempoDelta[LEFT] = 0;

    for (int tick = 0; tick < 192; ++tick) {
      arpClockPhase = tick;
      checkAdvanceArpeggiatorForSplit(LEFT);

      auto notes = arpNotes();
      int noteOns = 0;

      for (const auto& note : notes) {
        if ((note[0] & 0xF0) == 0x90) ++noteOns;
      }

      CHECK_EQ(noteOns, tick % rate.pulses == 0 ? 1 : 0);
    }
  }
}

TEST(arpeggiator_half_swing_preserves_long_short_pattern) {
  holdTwoNotes();
  Global.arpDirection = ArpUp;
  Global.arpTempo = ArpHalfSwing;
  arpTempoDelta[LEFT] = 0;

  for (int tick = 0; tick < 192; ++tick) {
    arpClockPhase = tick;
    checkAdvanceArpeggiatorForSplit(LEFT);

    auto notes = arpNotes();
    int noteOns = 0;

    for (const auto& note : notes) {
      if ((note[0] & 0xF0) == 0x90) ++noteOns;
    }

    bool expected = tick == 0 || tick == 56 ||
                    tick == 96 || tick == 152;

    CHECK_EQ(noteOns, expected ? 1 : 0);
  }
}

TEST(arpeggiator_slow_mode_button_preserves_rate_variants) {
  struct RatePair {
    ArpeggiatorStepTempo normal;
    ArpeggiatorStepTempo slow;
  };

  const RatePair pairs[] = {
    {ArpEighth, ArpWhole},
    {ArpEighthTriplet, ArpWholeTriplet},
    {ArpSixteenth, ArpHalf},
    {ArpSixteenthTriplet, ArpHalfTriplet},
    {ArpSixteenthSwing, ArpHalfSwing},
    {ArpThirtysecond, ArpFourth},
    {ArpThirtysecondTriplet, ArpFourthTriplet}
  };

  for (const auto& pair : pairs) {
    linn::reset();
    linn::select(14, 2);

    Global.arpTempo = pair.normal;
    CHECK(!isArpeggiatorSlowMode());

    handleGlobalSettingNewTouch();
    CHECK_EQ((int)Global.arpTempo, (int)pair.slow);
    CHECK(isArpeggiatorSlowMode());

    handleGlobalSettingNewTouch();
    CHECK_EQ((int)Global.arpTempo, (int)pair.normal);
    CHECK(!isArpeggiatorSlowMode());
  }
}

TEST(arpeggiator_midi_clock_start_and_192_tick_wrap) {
  linn::reset();

  linn::midiIn({0xFA});  // MIDI Start
  CHECK(arpClockNeedsSync);

  for (int tick = 0; tick < 385; ++tick) {
    linn::midiIn({0xF8});  // MIDI Timing Clock

    CHECK_EQ((int)arpClockPhase, tick % 192);
    CHECK(!arpClockNeedsSync);

    // The same MIDI clock position must not be processed twice.
    CHECK(!checkUpdateClock(micros()));
    CHECK_EQ((int)arpClockPhase, tick % 192);
  }

  linn::midiIn({0xFC});  // MIDI Stop
  CHECK_EQ((int)arpClockPhase, 0);
  CHECK(arpClockNeedsSync);
}

TEST(arpeggiator_midi_song_position_and_continue) {
  const int positions[] = {0, 9, 31, 33};

  for (int pos : positions) {
    linn::reset();

    // SPP counts six MIDI clock pulses per unit.
    linn::midiIn({0xF2, (uint8_t)(pos & 0x7F),
                        (uint8_t)(pos >> 7)});
    CHECK_EQ((int)arpClockPhase, (pos * 6) % 192);
    CHECK(arpClockNeedsSync);

    linn::midiIn({0xFB}); // MIDI Continue
    linn::midiIn({0xF8}); // First clock at the SPP position

    CHECK_EQ((int)arpClockPhase, (pos * 6) % 192);
    CHECK(!arpClockNeedsSync);

    for (int tick = 1; tick <= 192; ++tick) {
      linn::midiIn({0xF8});
      CHECK_EQ((int)arpClockPhase, (pos * 6 + tick) % 192);
    }
  }

  // Continue without a preceding SPP follows the firmware's
  // existing behavior of restarting at position zero.
  linn::reset();
  linn::midiIn({0xFB});
  linn::midiIn({0xF8});
  CHECK_EQ((int)arpClockPhase, 0);
}

TEST(arpeggiator_internal_clock_extends_to_192_ticks) {
  linn::reset();

  midiClockStatus = midiClockOff;
  arpClockNeedsSync = false;
  initializeClock();

  fxd4CurrentTempo = FXD4_FROM_INT(100);
  resetClockAdvancement(micros());

  CHECK(!isSyncedToMidiClock());

  for (int tick = 1; tick <= 384; ++tick) {
    host::advanceMicros(25000);

    unsigned long now = micros();

    CHECK(checkUpdateClock(now));
    CHECK_EQ((int)arpClockPhase, tick % 192);
    CHECK_EQ((int)clock24PPQ, tick % 24);

    // Calling again without advancing time must not count twice.
    CHECK(!checkUpdateClock(now));
    CHECK_EQ((int)arpClockPhase, tick % 192);
  }
}

TEST(arpeggiator_midi_clock_long_term_alignment) {
  linn::reset();
  linn::midiIn({0xFA}); // Start

  // 4096 clock messages: more than 42 measures of 4/4.
  for (int tick = 0; tick < 4096; ++tick) {
    linn::midiIn({0xF8});

    CHECK_EQ((int)clock24PPQ, tick % 24);
    CHECK_EQ((int)arpClockPhase, tick % 192);
  }

  linn::midiIn({0xFC}); // Stop
}

TEST(arpeggiator_skipped_clock_positions) {
  linn::reset();
  linn::midiIn({0xFA});
  linn::midiIn({0xF8});

  CHECK_EQ((int)arpClockPhase, 0);

  // Simulate the MIDI clock position advancing eight ticks
  // before checkUpdateClock() processes it.
  midiClockMessageCount = 9;

  CHECK(checkUpdateClock(micros()));
  CHECK_EQ((int)clock24PPQ, 8);
  CHECK_EQ((int)arpClockPhase, 8);

  // The following normal MIDI tick should advance by one.
  linn::midiIn({0xF8});
  CHECK_EQ((int)clock24PPQ, 9);
  CHECK_EQ((int)arpClockPhase, 9);

  linn::midiIn({0xFC});
}

TEST(arpeggiator_latch_survives_global_settings) {
  holdTwoNotes();

  Global.arpDirection = ArpUp;
  switchTargetEnabled[LEFT][ASSIGNED_LATCH] = true;

  // Release the physical pads; Latch should retain both notes.
  linn::release(5, 3);
  linn::release(7, 3);
  arpNotes();

  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);

  // Enter Global Settings through the normal touch handler.
  linn::press(0, GLOBAL_SETTINGS_ROW, 2000, 2000, 400);

  CHECK_EQ(displayMode, displayGlobal);
  CHECK(isSwitchLatchPressed(LEFT));
  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);

  // Verify the arpeggiator can still produce a note.
  advanceArpeggiatorForSplit(LEFT);

  auto notes = arpNotes();
  int noteOns = 0;
  for (const auto& note : notes) {
    if ((note[0] & 0xF0) == 0x90) ++noteOns;
  }
  CHECK_EQ(noteOns, 1);

  // Leaving Global Settings must preserve the latched notes.
  linn::release(0, GLOBAL_SETTINGS_ROW);
  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);
}

TEST(arpeggiator_global_settings_cleans_unlatched_split) {
  holdTwoNotes();

  // Left split: latched arpeggiator.
  switchTargetEnabled[LEFT][ASSIGNED_LATCH] = true;
  linn::release(5, 3);
  linn::release(7, 3);
  arpNotes();

  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);

  // Right split: unlatched arpeggiator with two notes.
  Global.splitActive = true;
  Split[RIGHT].arpeggiator = true;
  switchTargetEnabled[RIGHT][ASSIGNED_LATCH] = false;

  // Use a direct mapping setup to avoid assumptions about
  // the instrument's split-point coordinates.
  noteTouchMapping[RIGHT].noteOn(60, 1, 0, 0);
  noteTouchMapping[RIGHT].noteOn(64, 1, 0, 0);

  CHECK_EQ(noteTouchMapping[RIGHT].noteCount, 2);

  linn::press(0, GLOBAL_SETTINGS_ROW, 2000, 2000, 400);

  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);
  CHECK_EQ(noteTouchMapping[RIGHT].noteCount, 0);
  CHECK(isSwitchLatchPressed(LEFT));
  CHECK(!isSwitchLatchPressed(RIGHT));
}

TEST(arpeggiator_latch_all_notes_off_in_global_settings) {
  holdTwoNotes();

  switchTargetEnabled[LEFT][ASSIGNED_LATCH] = true;

  // Release the physical notes while Latch is active.
  linn::release(5, 3);
  linn::release(7, 3);
  arpNotes();

  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);

  // Enter Global Settings.
  linn::press(0, GLOBAL_SETTINGS_ROW, 2000, 2000, 400);
  CHECK_EQ(displayMode, displayGlobal);
  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);

  // Press the All Notes Off button in Global Settings.
  linn::press(16, 0, 2000, 2000, 400);
  linn::release(16, 0);

  // The stored notes and playback state must be cleared.
  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 0);
  CHECK_EQ(playingArpNote[LEFT], -1);
  CHECK_EQ(stepArpNote[LEFT], -1);

  // Latch remains enabled.
  CHECK(isSwitchLatchPressed(LEFT));

  // Advancing the arpeggiator must not restart the old notes.
  arpNotes();
  advanceArpeggiatorForSplit(LEFT);

  auto notes = arpNotes();
  int noteOns = 0;
  for (const auto& note : notes) {
    if ((note[0] & 0xF0) == 0x90 && note[2] > 0) {
      ++noteOns;
    }
  }
  CHECK_EQ(noteOns, 0);

  linn::release(0, GLOBAL_SETTINGS_ROW);
}

TEST(arpeggiator_legato_survives_global_settings) {
  holdTwoNotes();

  switchTargetEnabled[LEFT][ASSIGNED_LEGATO] = true;

  linn::release(5, 3);
  linn::release(7, 3);
  arpNotes();

  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);

  linn::press(0, GLOBAL_SETTINGS_ROW, 2000, 2000, 400);

  CHECK_EQ(displayMode, displayGlobal);
  CHECK(isSwitchLegatoPressed(LEFT));
  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);

  // Discard MIDI generated while entering Global Settings.
  arpNotes();

  advanceArpeggiatorForSplit(LEFT);

  auto notes = arpNotes();
  int noteOns = 0;
  for (const auto& note : notes) {
    if ((note[0] & 0xF0) == 0x90 && note[2] > 0) {
      ++noteOns;
    }
  }
  CHECK(noteOns > 0);

  linn::release(0, GLOBAL_SETTINGS_ROW);
  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);
}

TEST(arpeggiator_legato_replaces_previous_notes) {
  holdTwoNotes();

  switchTargetEnabled[LEFT][ASSIGNED_LEGATO] = true;

  // Release the original two notes.
  linn::release(5, 3);
  linn::release(7, 3);
  arpNotes();

  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);

  // Play a new note. Legato should discard the old captured notes.
  linn::press(9, 3, 2000, 2000, 400);
  arpNotes();

  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 1);

  // The replacement note remains captured after release.
  linn::release(9, 3);
  arpNotes();

  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 1);
}

TEST(arpeggiator_legato_survives_octave_transpose_menu) {
  holdTwoNotes();

  switchTargetEnabled[LEFT][ASSIGNED_LEGATO] = true;

  linn::release(5, 3);
  linn::release(7, 3);
  arpNotes();

  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);

  // Enter Octave/Transpose.
  linn::press(0, OCTAVE_ROW, 2000, 2000, 400);

  CHECK_EQ(displayMode, displayOctaveTranspose);
  CHECK(isSwitchLegatoPressed(LEFT));
  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);

  arpNotes();
  advanceArpeggiatorForSplit(LEFT);

  auto notes = arpNotes();
  int noteOns = 0;
  for (const auto& note : notes) {
    if ((note[0] & 0xF0) == 0x90 && note[2] > 0) {
      ++noteOns;
    }
  }
  CHECK(noteOns > 0);

  // Leaving the menu must retain the captured notes.
  linn::release(0, OCTAVE_ROW);
  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);
}

TEST(arpeggiator_legato_survives_volume_menu) {
  holdTwoNotes();

  switchTargetEnabled[LEFT][ASSIGNED_LEGATO] = true;

  linn::release(5, 3);
  linn::release(7, 3);
  arpNotes();

  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);

  // Enter Volume settings.
  linn::press(0, VOLUME_ROW, 2000, 2000, 400);

  CHECK_EQ(displayMode, displayVolume);
  CHECK(isSwitchLegatoPressed(LEFT));
  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);

  // Verify arpeggiator playback continues.
  arpNotes();
  advanceArpeggiatorForSplit(LEFT);

  auto notes = arpNotes();
  int noteOns = 0;
  for (const auto& note : notes) {
    if ((note[0] & 0xF0) == 0x90 && note[2] > 0) {
      ++noteOns;
    }
  }
  CHECK(noteOns > 0);

  // Leaving Volume must retain the captured notes.
  linn::release(0, VOLUME_ROW);
  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);
}

TEST(arpeggiator_legato_survives_split_menu) {
  holdTwoNotes();

  switchTargetEnabled[LEFT][ASSIGNED_LEGATO] = true;

  linn::release(5, 3);
  linn::release(7, 3);
  arpNotes();

  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);

  // Enter the Split menu.
  linn::press(0, SPLIT_ROW, 2000, 2000, 400);

  CHECK_EQ(displayMode, displaySplitPoint);
  CHECK(isSwitchLegatoPressed(LEFT));
  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);

  // Verify that playback continues.
  arpNotes();
  advanceArpeggiatorForSplit(LEFT);

  auto notes = arpNotes();
  int noteOns = 0;
  for (const auto& note : notes) {
    if ((note[0] & 0xF0) == 0x90 && note[2] > 0) {
      ++noteOns;
    }
  }
  CHECK(noteOns > 0);

  // Leaving Split must retain the captured notes.
  linn::release(0, SPLIT_ROW);
  CHECK_EQ(noteTouchMapping[LEFT].noteCount, 2);
}
