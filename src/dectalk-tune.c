/*
 * dectalk-tune.c
 *
 * A small DECtalk launcher for experimenting with voice parameters beyond
 * the normal speaker/rate controls exposed by the bundled `say` sample.
 */

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dtk/ttsapi.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define MAX_ACTIONS 128
#define TEXT_CHUNK 4096

static DWORD dev_encoding[3] = {
    WAVE_FORMAT_1M16,
    WAVE_FORMAT_1M08,
    WAVE_FORMAT_08M08
};

typedef struct FieldDef_tag {
    const char *name;
    const char *alias1;
    const char *alias2;
    size_t offset;
    const char *description;
} FieldDef;

#define FIELD(member, a1, a2, desc) { #member, a1, a2, offsetof(SPDEFS, member), desc }

static const FieldDef fields[] = {
    FIELD(sex, "gender", NULL, "1 male, 0 female"),
    FIELD(smoothness, "smooth", NULL, "voice smoothness, percent"),
    FIELD(assertiveness, "assert", NULL, "assertiveness, percent"),
    FIELD(average_pitch, "pitch", "ap", "average pitch, Hz"),
    FIELD(pitch_range, "range", "pr", "pitch range, percent"),
    FIELD(breathiness, "breath", "br", "breathiness, dB"),
    FIELD(richness, "rich", "ri", "richness, percent"),
    FIELD(num_fixed_samp_og, "open_glottis_samples", "og", "fixed samples of open glottis"),
    FIELD(laryngealization, "roughness", "larynx", "laryngealization, percent"),
    FIELD(head_size, "head", "hs", "head size, percent"),
    FIELD(formant4_res_freq, "f4", NULL, "fourth formant resonance frequency, Hz"),
    FIELD(formant4_bandwidth, "f4_bw", NULL, "fourth formant bandwidth, Hz"),
    FIELD(formant5_res_freq, "f5", NULL, "fifth formant resonance frequency, Hz"),
    FIELD(formant5_bandwidth, "f5_bw", NULL, "fifth formant bandwidth, Hz"),
    FIELD(parallel4_freq, "p4", NULL, "parallel fourth formant frequency, Hz"),
    FIELD(parallel5_freq, "p5", NULL, "parallel fifth formant frequency, Hz"),
    FIELD(gain_frication, "frication", "gf", "gain of frication source, dB"),
    FIELD(gain_aspiration, "aspiration", "ga", "gain of aspiration source, dB"),
    FIELD(gain_voicing, "voicing", "gv", "gain of voicing source, dB"),
    FIELD(gain_nasalization, "nasal", "gn", "gain of nasalization, dB"),
    FIELD(gain_cfr1, "cfr1", NULL, "gain of cascade formant resonator 1, dB"),
    FIELD(gain_cfr2, "cfr2", NULL, "gain of cascade formant resonator 2, dB"),
    FIELD(gain_cfr3, "cfr3", NULL, "gain of cascade formant resonator 3, dB"),
    FIELD(gain_cfr4, "cfr4", NULL, "gain of cascade formant resonator 4, dB"),
    FIELD(loudness, "loud", NULL, "loudness, dB"),
    FIELD(spectral_tilt, "tilt", NULL, "spectral tilt, percent"),
    FIELD(baseline_fall, "fall", NULL, "baseline fall, Hz"),
    FIELD(lax_breathiness, "lax_breath", NULL, "lax breathiness, percent"),
    FIELD(quickness, "quick", NULL, "quickness, percent"),
    FIELD(hat_rise, "hat", NULL, "hat rise, Hz"),
    FIELD(stress_rise, "stress", NULL, "stress rise, Hz"),
    FIELD(avg_glot_open, "glot_open", NULL, "average glottal opening"),
    FIELD(avg_glot_voicd_open, "glot_voiced", NULL, "average voiced glottal opening"),
    FIELD(avg_glot_unv_open, "glot_unvoiced", NULL, "average unvoiced glottal opening"),
    FIELD(area_chink, "chink", NULL, "glottal chink area"),
    FIELD(open_quo, "open_quotient", "oq", "open quotient"),
    FIELD(output_gain_mult, "gain", NULL, "output gain multiplier")
};

typedef enum ActionType_tag {
    ACTION_PRESET,
    ACTION_PARAM
} ActionType;

typedef struct Action_tag {
    ActionType type;
    const char *value;
} Action;

typedef struct Options_tag {
    const char *lang;
    const char *voice;
    int speaker;
    int rate;
    int volume;
    const char *out_file;
    int encoding;
    const char *input_file;
    const char *pre_text;
    const char *post_text;
    int dump_params;
    int dump_only;
    int quiet;
    Action actions[MAX_ACTIONS];
    int action_count;
    char **text_parts;
    int text_count;
} Options;

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s [options] \"text to speak\"\n"
        "  echo \"text\" | %s [options]\n"
        "\n"
        "Options:\n"
        "  --voice, -s NAME|N       Voice/speaker alias or number.\n"
        "  --rate, -r N             Speaking rate. Values over 600 are clamped.\n"
        "  --volume, -v N           Volume 0-100.\n"
        "  --lang, -l CODE          Language: us, uk, sp, gr/de, la, fr.\n"
        "  --out, -o FILE           Write wave audio to FILE.\n"
        "  --encoding, -e N         For --out: 1=16-bit PCM, 2=8-bit PCM, 3=mulaw.\n"
        "  --preset NAME            Apply a voice-parameter preset. Repeatable.\n"
        "  --param NAME=VALUE       Set a SPDEFS parameter. Repeatable.\n"
        "                           Also supports NAME+=N and NAME-=N.\n"
        "  --file, -f FILE          Speak input text from FILE.\n"
        "  --pre TEXT               Speak DECtalk command/text before input.\n"
        "  --post TEXT              Speak DECtalk command/text after input.\n"
        "  --dump-params            Print current/default/limit parameters.\n"
        "  --dump-only              Print parameters and exit without speaking.\n"
        "  --list-params            List tweakable parameter names.\n"
        "  --list-presets           List built-in presets.\n"
        "  --help, -h               Show this help.\n"
        "\n"
        "Examples:\n"
        "  %s --out robot.wav --preset robot \"Greetings, human.\"\n"
        "  %s --voice paul --rate 600 --param pitch=95 --param head=140 \"Deep voice.\"\n",
        prog, prog, prog, prog);
}

static void print_presets(void)
{
    puts("default   leave the selected speaker mostly alone");
    puts("robot     flatter/cleaner synthetic voice");
    puts("giant     lower pitch, larger head size");
    puts("chipmunk  higher pitch, smaller head size");
    puts("whisper   breathy/aspirated voice");
    puts("rough     more laryngealized/raspy voice");
    puts("flat      minimal pitch movement");
    puts("bright    brighter, smaller, more assertive voice");
    puts("dark      darker, larger, less bright voice");
}

static void print_params(void)
{
    int i;
    for (i = 0; i < ARRAY_LEN(fields); i++) {
        printf("%-24s", fields[i].name);
        if (fields[i].alias1 || fields[i].alias2) {
            printf(" aliases:");
            if (fields[i].alias1) printf(" %s", fields[i].alias1);
            if (fields[i].alias2) printf(" %s", fields[i].alias2);
        }
        printf("\n    %s\n", fields[i].description);
    }
}

static int streq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int voice_to_id(const char *voice, int *speaker)
{
    char *end = NULL;
    long n;

    if (!voice || !*voice) return 0;
    errno = 0;
    n = strtol(voice, &end, 10);
    if (errno == 0 && end && *end == '\0' && n >= 0 && n <= 9) {
        *speaker = (int)n;
        return 1;
    }

    if (streq_ci(voice, "paul") || streq_ci(voice, "pablo")) *speaker = 0;
    else if (streq_ci(voice, "betty") || streq_ci(voice, "berta") || streq_ci(voice, "beate")) *speaker = 1;
    else if (streq_ci(voice, "harry") || streq_ci(voice, "humberto") || streq_ci(voice, "hans")) *speaker = 2;
    else if (streq_ci(voice, "frank") || streq_ci(voice, "francisco")) *speaker = 3;
    else if (streq_ci(voice, "dennis") || streq_ci(voice, "domingo") || streq_ci(voice, "dieter")) *speaker = 4;
    else if (streq_ci(voice, "kit") || streq_ci(voice, "kid") || streq_ci(voice, "juanito") || streq_ci(voice, "karl")) *speaker = 5;
    else if (streq_ci(voice, "ursula")) *speaker = 6;
    else if (streq_ci(voice, "rita")) *speaker = 7;
    else if (streq_ci(voice, "wendy")) *speaker = 8;
    else return 0;

    return 1;
}

static const FieldDef *find_field(const char *name)
{
    int i;
    for (i = 0; i < ARRAY_LEN(fields); i++) {
        if (streq_ci(name, fields[i].name)) return &fields[i];
        if (fields[i].alias1 && streq_ci(name, fields[i].alias1)) return &fields[i];
        if (fields[i].alias2 && streq_ci(name, fields[i].alias2)) return &fields[i];
    }
    return NULL;
}

static short *field_ptr(SPDEFS *defs, const FieldDef *field)
{
    return (short *)((char *)defs + field->offset);
}

static const short *field_cptr(const SPDEFS *defs, const FieldDef *field)
{
    return (const short *)((const char *)defs + field->offset);
}

static int add_action(Options *opt, ActionType type, const char *value)
{
    if (opt->action_count >= MAX_ACTIONS) {
        fprintf(stderr, "dectalk-tune: too many --preset/--param actions\n");
        return 0;
    }
    opt->actions[opt->action_count].type = type;
    opt->actions[opt->action_count].value = value;
    opt->action_count++;
    return 1;
}

static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    long n;
    if (!s || !*s) return 0;
    errno = 0;
    n = strtol(s, &end, 10);
    if (errno != 0 || !end || *end != '\0') return 0;
    if (n < -32768 || n > 32767) return 0;
    *out = (int)n;
    return 1;
}

static short clamp_field_value(const FieldDef *field, int value, const SPDEFS *lo, const SPDEFS *hi, int quiet)
{
    int out = value;
    if (lo && hi) {
        int lov = *field_cptr(lo, field);
        int hiv = *field_cptr(hi, field);
        if (lov <= hiv) {
            if (out < lov) {
                if (!quiet) fprintf(stderr, "dectalk-tune: %s=%d clamped to low limit %d\n", field->name, out, lov);
                out = lov;
            }
            if (out > hiv) {
                if (!quiet) fprintf(stderr, "dectalk-tune: %s=%d clamped to high limit %d\n", field->name, out, hiv);
                out = hiv;
            }
        }
    }
    if (out < -32768) out = -32768;
    if (out > 32767) out = 32767;
    return (short)out;
}

static int set_field_value(SPDEFS *defs, const SPDEFS *lo, const SPDEFS *hi, const char *name, int value, int relative, int quiet)
{
    const FieldDef *field = find_field(name);
    short *target;
    int new_value;

    if (!field) {
        fprintf(stderr, "dectalk-tune: unknown parameter '%s'\n", name);
        fprintf(stderr, "dectalk-tune: run --list-params to see supported names\n");
        return 0;
    }

    target = field_ptr(defs, field);
    new_value = relative ? ((int)*target + value) : value;
    *target = clamp_field_value(field, new_value, lo, hi, quiet);
    return 1;
}

static int apply_param(SPDEFS *defs, const SPDEFS *lo, const SPDEFS *hi, const char *spec, int quiet)
{
    const char *op = NULL;
    char name[128];
    int value;
    int relative = 0;
    size_t len;

    op = strstr(spec, "+=");
    if (op) {
        relative = 1;
    } else {
        op = strstr(spec, "-=");
        if (op) relative = 1;
    }
    if (!op) op = strchr(spec, '=');
    if (!op) {
        fprintf(stderr, "dectalk-tune: --param must look like name=value, name+=N, or name-=N; got '%s'\n", spec);
        return 0;
    }

    len = (size_t)(op - spec);
    if (len == 0 || len >= sizeof(name)) {
        fprintf(stderr, "dectalk-tune: bad parameter name in '%s'\n", spec);
        return 0;
    }
    memcpy(name, spec, len);
    name[len] = '\0';

    if (!parse_int(op + (relative ? 2 : 1), &value)) {
        fprintf(stderr, "dectalk-tune: bad parameter value in '%s'\n", spec);
        return 0;
    }
    if (relative && op[0] == '-') value = -value;

    return set_field_value(defs, lo, hi, name, value, relative, quiet);
}

static int apply_preset(SPDEFS *defs, const SPDEFS *lo, const SPDEFS *hi, const char *preset, int quiet)
{
#define SET(name, value) do { if (!set_field_value(defs, lo, hi, name, value, 0, quiet)) return 0; } while (0)
#define ADD(name, value) do { if (!set_field_value(defs, lo, hi, name, value, 1, quiet)) return 0; } while (0)

    if (streq_ci(preset, "default") || streq_ci(preset, "none")) {
        return 1;
    } else if (streq_ci(preset, "robot")) {
        SET("smoothness", 0);
        SET("assertiveness", 100);
        SET("pitch_range", 15);
        SET("breathiness", 0);
        SET("laryngealization", 0);
        SET("richness", 100);
        SET("quickness", 120);
    } else if (streq_ci(preset, "giant") || streq_ci(preset, "monster")) {
        SET("average_pitch", 75);
        SET("pitch_range", 35);
        SET("head_size", 180);
        SET("richness", 100);
        SET("breathiness", 0);
        SET("quickness", 85);
    } else if (streq_ci(preset, "chipmunk") || streq_ci(preset, "tiny")) {
        SET("average_pitch", 260);
        SET("pitch_range", 160);
        SET("head_size", 55);
        SET("quickness", 130);
        SET("richness", 40);
    } else if (streq_ci(preset, "whisper") || streq_ci(preset, "breathy")) {
        SET("breathiness", 80);
        SET("lax_breathiness", 80);
        SET("gain_aspiration", 70);
        SET("gain_voicing", 35);
        SET("richness", 25);
        SET("loudness", 35);
    } else if (streq_ci(preset, "rough") || streq_ci(preset, "raspy")) {
        SET("smoothness", 0);
        SET("laryngealization", 85);
        SET("spectral_tilt", 85);
        SET("breathiness", 20);
        SET("richness", 90);
    } else if (streq_ci(preset, "flat") || streq_ci(preset, "monotone")) {
        SET("pitch_range", 0);
        SET("hat_rise", 0);
        SET("stress_rise", 0);
        SET("baseline_fall", 0);
        SET("assertiveness", 20);
    } else if (streq_ci(preset, "bright")) {
        ADD("average_pitch", 25);
        SET("head_size", 75);
        SET("richness", 35);
        SET("spectral_tilt", 30);
        SET("assertiveness", 90);
    } else if (streq_ci(preset, "dark")) {
        ADD("average_pitch", -25);
        SET("head_size", 135);
        SET("richness", 100);
        SET("spectral_tilt", 90);
        SET("assertiveness", 25);
    } else {
        fprintf(stderr, "dectalk-tune: unknown preset '%s'\n", preset);
        fprintf(stderr, "dectalk-tune: run --list-presets to see built-ins\n");
        return 0;
    }
    return 1;

#undef SET
#undef ADD
}

static void dump_speaker_params(const SPDEFS *cur, const SPDEFS *lo, const SPDEFS *hi, const SPDEFS *def)
{
    int i;
    printf("%-24s %8s %8s %8s %8s\n", "parameter", "current", "default", "low", "high");
    printf("%-24s %8s %8s %8s %8s\n", "---------", "-------", "-------", "---", "----");
    for (i = 0; i < ARRAY_LEN(fields); i++) {
        printf("%-24s %8d %8d %8d %8d\n",
               fields[i].name,
               cur ? (int)*field_cptr(cur, &fields[i]) : 0,
               def ? (int)*field_cptr(def, &fields[i]) : 0,
               lo ? (int)*field_cptr(lo, &fields[i]) : 0,
               hi ? (int)*field_cptr(hi, &fields[i]) : 0);
    }
}

static char *read_stream(FILE *fp)
{
    char *buf = NULL;
    size_t cap = 0;
    size_t len = 0;

    for (;;) {
        size_t got;
        if (len + TEXT_CHUNK + 1 > cap) {
            size_t new_cap = cap ? cap * 2 : TEXT_CHUNK + 1;
            char *tmp;
            while (new_cap < len + TEXT_CHUNK + 1) new_cap *= 2;
            tmp = (char *)realloc(buf, new_cap);
            if (!tmp) {
                free(buf);
                return NULL;
            }
            buf = tmp;
            cap = new_cap;
        }
        got = fread(buf + len, 1, TEXT_CHUNK, fp);
        len += got;
        if (got < TEXT_CHUNK) {
            if (ferror(fp)) {
                free(buf);
                return NULL;
            }
            break;
        }
    }

    if (!buf) {
        buf = (char *)malloc(1);
        if (!buf) return NULL;
    }
    buf[len] = '\0';
    return buf;
}

static char *join_text(char **parts, int count)
{
    size_t len = 0;
    int i;
    char *out;

    for (i = 0; i < count; i++) len += strlen(parts[i]) + 1;
    out = (char *)malloc(len + 1);
    if (!out) return NULL;
    out[0] = '\0';
    for (i = 0; i < count; i++) {
        if (i) strcat(out, " ");
        strcat(out, parts[i]);
    }
    return out;
}

static int speak_text(LPTTS_HANDLE_T handle, const char *text)
{
    if (!text || !*text) return 1;
    if (TextToSpeechSpeak(handle, (char *)text, TTS_FORCE) != MMSYSERR_NOERROR) {
        fprintf(stderr, "dectalk-tune: TextToSpeechSpeak failed\n");
        return 0;
    }
    return 1;
}

static int need_value(int argc, char **argv, int i)
{
    return i + 1 < argc && argv[i + 1][0] != '\0';
}

int main(int argc, char **argv)
{
    Options opt;
    LPTTS_HANDLE_T handle = NULL;
    SPDEFS *cur = NULL;
    SPDEFS *lo = NULL;
    SPDEFS *hi = NULL;
    SPDEFS *def = NULL;
    SPDEFS tuned;
    int have_params = 0;
    DWORD dev_options = WAVE_OPEN_SHAREABLE;
    unsigned int tts_lang = 0;
    MMRESULT status;
    char *input_text = NULL;
    int i;
    int rc = 1;

    memset(&opt, 0, sizeof(opt));
    /* Leave language unset by default so DECtalk uses DECtalk.conf's Default_lang.
       Calling TextToSpeechStartLang("us") on some installs loads optional audio
       glue even when writing straight to a wave file. */
    opt.lang = NULL;
    opt.voice = "paul";
    opt.speaker = 0;
    opt.rate = 180;
    opt.volume = 90;
    opt.encoding = 1;
    opt.text_parts = argv;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--list-params") == 0) {
            print_params();
            return 0;
        } else if (strcmp(argv[i], "--list-presets") == 0) {
            print_presets();
            return 0;
        } else if (strcmp(argv[i], "--voice") == 0 || strcmp(argv[i], "-s") == 0) {
            if (!need_value(argc, argv, i)) { fprintf(stderr, "dectalk-tune: %s needs a value\n", argv[i]); return 2; }
            opt.voice = argv[++i];
        } else if (strcmp(argv[i], "--rate") == 0 || strcmp(argv[i], "-r") == 0) {
            if (!need_value(argc, argv, i) || !parse_int(argv[i + 1], &opt.rate)) { fprintf(stderr, "dectalk-tune: --rate needs a number\n"); return 2; }
            i++;
        } else if (strcmp(argv[i], "--volume") == 0 || strcmp(argv[i], "-v") == 0) {
            if (!need_value(argc, argv, i) || !parse_int(argv[i + 1], &opt.volume)) { fprintf(stderr, "dectalk-tune: --volume needs a number\n"); return 2; }
            i++;
        } else if (strcmp(argv[i], "--lang") == 0 || strcmp(argv[i], "-l") == 0) {
            if (!need_value(argc, argv, i)) { fprintf(stderr, "dectalk-tune: %s needs a value\n", argv[i]); return 2; }
            opt.lang = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 || strcmp(argv[i], "-o") == 0) {
            if (!need_value(argc, argv, i)) { fprintf(stderr, "dectalk-tune: %s needs a value\n", argv[i]); return 2; }
            opt.out_file = argv[++i];
        } else if (strcmp(argv[i], "--encoding") == 0 || strcmp(argv[i], "-e") == 0) {
            if (!need_value(argc, argv, i) || !parse_int(argv[i + 1], &opt.encoding)) { fprintf(stderr, "dectalk-tune: --encoding needs 1, 2, or 3\n"); return 2; }
            i++;
        } else if (strcmp(argv[i], "--preset") == 0) {
            if (!need_value(argc, argv, i)) { fprintf(stderr, "dectalk-tune: --preset needs a value\n"); return 2; }
            if (!add_action(&opt, ACTION_PRESET, argv[++i])) return 2;
        } else if (strcmp(argv[i], "--param") == 0 || strcmp(argv[i], "-p") == 0) {
            if (!need_value(argc, argv, i)) { fprintf(stderr, "dectalk-tune: %s needs name=value\n", argv[i]); return 2; }
            if (!add_action(&opt, ACTION_PARAM, argv[++i])) return 2;
        } else if (strcmp(argv[i], "--file") == 0 || strcmp(argv[i], "-f") == 0) {
            if (!need_value(argc, argv, i)) { fprintf(stderr, "dectalk-tune: %s needs a value\n", argv[i]); return 2; }
            opt.input_file = argv[++i];
        } else if (strcmp(argv[i], "--pre") == 0) {
            if (!need_value(argc, argv, i)) { fprintf(stderr, "dectalk-tune: --pre needs text\n"); return 2; }
            opt.pre_text = argv[++i];
        } else if (strcmp(argv[i], "--post") == 0) {
            if (!need_value(argc, argv, i)) { fprintf(stderr, "dectalk-tune: --post needs text\n"); return 2; }
            opt.post_text = argv[++i];
        } else if (strcmp(argv[i], "--dump-params") == 0) {
            opt.dump_params = 1;
        } else if (strcmp(argv[i], "--dump-only") == 0) {
            opt.dump_params = 1;
            opt.dump_only = 1;
        } else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            opt.quiet = 1;
        } else if (strcmp(argv[i], "--") == 0) {
            i++;
            while (i < argc) opt.text_parts[opt.text_count++] = argv[i++];
            break;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "dectalk-tune: unknown option '%s'\n", argv[i]);
            return 2;
        } else {
            opt.text_parts[opt.text_count++] = argv[i];
        }
    }

    if (!voice_to_id(opt.voice, &opt.speaker)) {
        fprintf(stderr, "dectalk-tune: unknown voice '%s'\n", opt.voice);
        return 2;
    }

    if (opt.rate < 75) {
        if (!opt.quiet) fprintf(stderr, "dectalk-tune: rate %d clamped to 75\n", opt.rate);
        opt.rate = 75;
    }
    if (opt.rate > 600) {
        if (!opt.quiet) fprintf(stderr, "dectalk-tune: rate %d is above DECtalk's speaking limit; clamped to 600\n", opt.rate);
        opt.rate = 600;
    }
    if (opt.volume < 0) opt.volume = 0;
    if (opt.volume > 100) opt.volume = 100;
    if (opt.encoding < 1 || opt.encoding > 3) {
        fprintf(stderr, "dectalk-tune: encoding must be 1, 2, or 3\n");
        return 2;
    }

    if (opt.lang && streq_ci(opt.lang, "de")) opt.lang = "gr";
    if (opt.lang && *opt.lang) {
        tts_lang = TextToSpeechStartLang((char *)opt.lang);
        if (tts_lang & TTS_LANG_ERROR) {
            fprintf(stderr, "dectalk-tune: language '%s' is not available/supported\n", opt.lang);
            return 1;
        }
        TextToSpeechSelectLang(NULL, tts_lang);
    }

    if (opt.out_file) dev_options = DO_NOT_USE_AUDIO_DEVICE;

    status = TextToSpeechStartup(&handle, WAVE_MAPPER, dev_options, NULL, (long)NULL);
    if (status != MMSYSERR_NOERROR) {
        fprintf(stderr, "dectalk-tune: TextToSpeechStartup failed with code %d\n", status);
        return 1;
    }

    TextToSpeechSetSpeaker(handle, (SPEAKER_T)opt.speaker);
    TextToSpeechSetRate(handle, (DWORD)opt.rate);
    TextToSpeechSetVolume(handle, VOLUME_MAIN, opt.volume);

    status = TextToSpeechGetSpeakerParams(handle, (UINT)opt.speaker, &cur, &lo, &hi, &def);
    if (status == MMSYSERR_NOERROR && cur) {
        tuned = *cur;
        have_params = 1;
        for (i = 0; i < opt.action_count; i++) {
            if (opt.actions[i].type == ACTION_PRESET) {
                if (!apply_preset(&tuned, lo, hi, opt.actions[i].value, opt.quiet)) goto cleanup;
            } else {
                if (!apply_param(&tuned, lo, hi, opt.actions[i].value, opt.quiet)) goto cleanup;
            }
        }
        if (opt.action_count > 0) {
            status = TextToSpeechSetSpeakerParams(handle, &tuned);
            if (status != MMSYSERR_NOERROR) {
                fprintf(stderr, "dectalk-tune: TextToSpeechSetSpeakerParams failed with code %d\n", status);
                goto cleanup;
            }
            cur = &tuned;
        }
    } else if (opt.action_count > 0 || opt.dump_params) {
        fprintf(stderr, "dectalk-tune: TextToSpeechGetSpeakerParams failed with code %d\n", status);
        goto cleanup;
    }

    if (opt.dump_params && have_params) {
        dump_speaker_params(cur, lo, hi, def);
        if (opt.dump_only) {
            rc = 0;
            goto cleanup;
        }
    }

    if (opt.out_file) {
        status = TextToSpeechOpenWaveOutFile(handle, (char *)opt.out_file, dev_encoding[opt.encoding - 1]);
        if (status != MMSYSERR_NOERROR) {
            fprintf(stderr, "dectalk-tune: could not open output file '%s' with code %d\n", opt.out_file, status);
            goto cleanup;
        }
    }

    if (opt.input_file) {
        FILE *fp = fopen(opt.input_file, "rb");
        if (!fp) {
            fprintf(stderr, "dectalk-tune: cannot open '%s': %s\n", opt.input_file, strerror(errno));
            goto cleanup;
        }
        input_text = read_stream(fp);
        fclose(fp);
    } else if (opt.text_count > 0) {
        input_text = join_text(opt.text_parts, opt.text_count);
    } else if (!isatty(STDIN_FILENO)) {
        input_text = read_stream(stdin);
    } else {
        print_usage(argv[0]);
        rc = 2;
        goto cleanup;
    }

    if (!input_text) {
        fprintf(stderr, "dectalk-tune: could not read input text\n");
        goto cleanup;
    }

    if (!speak_text(handle, opt.pre_text)) goto cleanup;
    if (!speak_text(handle, input_text)) goto cleanup;
    if (!speak_text(handle, opt.post_text)) goto cleanup;

    TextToSpeechSpeak(handle, "        ", TTS_FORCE);
    TextToSpeechSync(handle);

    if (opt.out_file) TextToSpeechCloseWaveOutFile(handle);
    rc = 0;

cleanup:
    free(input_text);
    if (handle) TextToSpeechShutdown(handle);
    return rc;
}
