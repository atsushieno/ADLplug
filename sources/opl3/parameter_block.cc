//          Copyright Jean Pierre Cimalando 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "parameter_block.h"
#include "adl/chip_settings.h"
#include "adl/instrument.h"
#include "adl/player.h"
#include "utility/pak.h"
#include "BinaryData.h"
#include <wopl/wopl_file.h>
#include <fmt/format.h>
#include <cassert>

static WOPLFile_Ptr default_wopl()
{
    Pak_File_Reader pak;
    if (!pak.init_with_data((const uint8_t *)BinaryData::banks_pak, BinaryData::banks_pakSize))
        assert(false);
    std::string default_wopl = pak.extract(0);
    assert(default_wopl.size() != 0);

    WOPLFile_Ptr file(WOPL_LoadBankFromMem((void *)default_wopl.data(), default_wopl.size(), nullptr));
    if (!file)
        throw std::bad_alloc();

    return file;
}

static Instrument default_instrument(const WOPLFile &file)
{
    WOPLBank *bank = nullptr;
    for (unsigned i = 0, n = file.banks_count_melodic; i < n && !bank; ++i) {
        WOPLBank *cur = &file.banks_melodic[i];
        if (cur->bank_midi_lsb == 0 && cur->bank_midi_msb == 0)
            bank = cur;
    }
    assert(bank);
    return Instrument::from_wopl(bank->ins[0]);
}

static Chip_Settings default_chip_settings()
{
    Chip_Settings cs;
    cs.emulator = ::get_emulator_defaults().default_index;
    return cs;
}

void Parameter_Block::setup_parameters(AudioProcessor &p)
{
    Chip_Settings cs = default_chip_settings();

    typedef AudioParameterType Pt;
    typedef NormalisableRange<float> Rf;

    p_mastervol = addAutomatableParameter<Pt::Float>(p, "mastervol", "Master volume", Rf{0.0f, 10.0f}, 1.0f, String());

    first_chip_setting = p.getParameters().size();
    StringArray emu_choices = get_emulator_defaults().choices;
    for (unsigned i = 0, n = emu_choices.size(); i < n; ++i) {
        if (emu_choices[i].isEmpty())
            emu_choices.set(i, "<Reserved " + String(i) + ">");
    }
    p_emulator = addParameter<Pt::Choice>(p, "emulator", "Emulator", emu_choices, cs.emulator, String());
    p_nchip = addParameter<Pt::Int>(p, "nchip", "Chip count", 1, 100, cs.chip_count, String());
    p_n4op = addParameter<Pt::Int>(p, "n4op", "4op channel count", 0, 600, cs.fourop_count, String());
    last_chip_setting = p.getParameters().size() - 1;

    WOPLFile_Ptr wopl = default_wopl();
    Instrument ins = default_instrument(*wopl);

    first_instrument_parameter = p.getParameters().size();
    for (unsigned pn = 0; pn < 16; ++pn) {
        Part &part = this->part[pn];

        {
            String idprefix = fmt::format("P{:d}", pn + 1);
            String nameprefix = fmt::format("[Part {:d}] ", pn + 1);

            auto id = [idprefix](const char *x) -> String { return idprefix + x; };
            auto name = [nameprefix](const char *x) -> String { return nameprefix + x; };

            part.p_is4op = addParameter<Pt::Bool>(p, id("is4op"), name("4op"), ins.four_op(), String());
            part.p_ps4op = addParameter<Pt::Bool>(p, id("ps4op"), name("Pseudo 4op"), ins.pseudo_four_op(), String());
            part.p_blank = addParameter<Pt::Bool>(p, id("blank"), name("Blank"), ins.blank(), String());
            StringArray con_choices = {"FM", "AM"};
            part.p_con12 = addParameter<Pt::Choice>(p, id("con12"), name("Mode 1-2"), con_choices, ins.con12(), String());
            part.p_con34 = addParameter<Pt::Choice>(p, id("con34"), name("Mode 3-4"), con_choices, ins.con34(), String());
            part.p_tune12 = addParameter<Pt::Int>(p, id("tune12"), name("Note offset 1-2"), -127, +127, ins.note_offset1, String());
            part.p_tune34 = addParameter<Pt::Int>(p, id("tune34"), name("Note offset 3-4"), -127, +127, ins.note_offset2, String());
            part.p_fb12 = addParameter<Pt::Int>(p, id("fb12"), name("Feedback 1-2"), 0, 7, ins.fb12(), String());
            part.p_fb34 = addParameter<Pt::Int>(p, id("fb34"), name("Feedback 3-4"), 0, 7, ins.fb34(), String());
            part.p_veloffset = addParameter<Pt::Int>(p, id("veloffset"), name("Velocity offset"), -127, +127, ins.midi_velocity_offset, String());
            part.p_voice2ft = addParameter<Pt::Int>(p, id("voice2ft"), name("Voice 2 fine tune"), -127, +127, ins.second_voice_detune, String());
            part.p_drumnote = addParameter<Pt::Int>(p, id("drumnote"), name("Percussion note"), 0, 127, ins.percussion_key_number, String());
        }

        for (unsigned opnum = 0; opnum < 4; ++opnum) {
            String idprefix = fmt::format(
                "P{:d}{:s}", pn + 1,
                ((const char *[]){ "c1", "m1", "c2", "m2" })[opnum]);
            String nameprefix = fmt::format(
                "[Part {:d}] {:s} ", pn + 1,
                ((const char *[]){ "Carrier 1", "Modulator 1", "Carrier 2", "Modulator 2" })[opnum]);

            auto id = [idprefix](const char *x) -> String { return idprefix + String(x); };
            auto name = [nameprefix](const char *x) -> String { return nameprefix + String(x); };

            Operator &op = part.nth_operator(opnum);
            op.p_attack = addParameter<Pt::Int>(p, id("attack"), name("Attack"), 0, 15, ins.attack(opnum), String());
            op.p_decay = addParameter<Pt::Int>(p, id("decay"), name("Decay"), 0, 15, ins.decay(opnum), String());
            op.p_sustain = addParameter<Pt::Int>(p, id("sustain"), name("Sustain"), 0, 15, ins.sustain(opnum), String());
            op.p_release = addParameter<Pt::Int>(p, id("release"), name("Release"), 0, 15, ins.release(opnum), String());
            op.p_level = addAutomatableParameter<Pt::Int>(p, id("level"), name("Level"), 0, 63, ins.level(opnum), String());
            op.p_ksl = addParameter<Pt::Int>(p, id("ksl"), name("Key scale level"), 0, 3, ins.ksl(opnum), String());
            op.p_fmul = addParameter<Pt::Int>(p, id("fmul"), name("Frequency multiplier"), 0, 15, ins.fmul(opnum), String());
            op.p_trem = addParameter<Pt::Bool>(p, id("trem"), name("Tremolo"), ins.trem(opnum), String());
            op.p_vib = addParameter<Pt::Bool>(p, id("vib"), name("Vibrato"), ins.vib(opnum), String());
            op.p_sus = addParameter<Pt::Bool>(p, id("sus"), name("Sustaining"), ins.sus(opnum), String());
            op.p_env = addParameter<Pt::Bool>(p, id("env"), name("Key scaling"), ins.env(opnum), String());
            StringArray waves {
                "Sine",
                "Half sine",
                "Absolute sine",
                "Pulse sine",
                "Alternating sine",
                "Camel sine",
                "Square",
                "Logarithmic sawtooth",
            };
            op.p_wave = addParameter<Pt::Choice>(p, id("wave"), name("Waveform"), waves, ins.wave(opnum), String());
        }
    }
    last_instrument_parameter = p.getParameters().size() - 1;

    first_global_parameter = p.getParameters().size();
    StringArray volmodel_choices = {"Generic", "Native", "DMX", "Apogee", "Win9x"};
    p_volmodel = addParameter<Pt::Choice>(p, "volmodel", "Volume model", volmodel_choices, wopl->volume_model, String());
    p_deeptrem = addParameter<Pt::Bool>(p, "deeptrem", "Deep tremolo", wopl->opl_flags & WOPL_FLAG_DEEP_TREMOLO, String());
    p_deepvib = addParameter<Pt::Bool>(p, "deepvib", "Deep vibrato", wopl->opl_flags & WOPL_FLAG_DEEP_VIBRATO, String());
    last_global_parameter = p.getParameters().size() - 1;
}
