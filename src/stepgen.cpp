/*
 * StepGen (STEP/DIR) - Implementation
 */

#include "stepgen.h"
#include <Arduino.h>
#include <string.h>

#if defined(ESP32)
#if __has_include(<driver/mcpwm.h>)
#define STEPGEN_HAS_MCPWM 1
#include <driver/mcpwm.h>
#else
#define STEPGEN_HAS_MCPWM 0
#endif
#else
#define STEPGEN_HAS_MCPWM 0
#endif

extern "C" {

static inline uint32_t stepgen_min_u32(uint32_t a, uint32_t b) { return (a < b) ? a : b; }

static float stepgen_step_duty_percent(const stepgen_axis_config_t* c) {
    if (!c) return 50.0f;
    const uint32 total_us = (uint32)c->step_time_us + (uint32)c->step_space_us;
    if (total_us == 0) return 50.0f;
    float duty = 100.0f * ((float)c->step_time_us / (float)total_us);
    if (duty < 1.0f) duty = 1.0f;
    if (duty > 99.0f) duty = 99.0f;
    return duty;
}

#if STEPGEN_HAS_MCPWM
static boolean stepgen_mcpwm_map_axis(uint8 axis, mcpwm_unit_t* unit, mcpwm_timer_t* timer, mcpwm_operator_t* opr, mcpwm_io_signals_t* sig) {
    if (!unit || !timer || !opr || !sig) return false;
    // IMPORTANT: each axis gets its own timer => independent frequency per axis
    switch (axis) {
        case 0:
            *unit = MCPWM_UNIT_0; *timer = MCPWM_TIMER_0; *opr = MCPWM_OPR_A; *sig = MCPWM0A; return true;
        case 1:
            *unit = MCPWM_UNIT_0; *timer = MCPWM_TIMER_1; *opr = MCPWM_OPR_A; *sig = MCPWM1A; return true;
        case 2:
            *unit = MCPWM_UNIT_0; *timer = MCPWM_TIMER_2; *opr = MCPWM_OPR_A; *sig = MCPWM2A; return true;
        case 3:
            *unit = MCPWM_UNIT_1; *timer = MCPWM_TIMER_0; *opr = MCPWM_OPR_A; *sig = MCPWM0A; return true;
        default:
            break;
    }
    return false;
}

static boolean s_stepgen_mcpwm_timer_inited[2][3] = {{false, false, false}, {false, false, false}};

static void stepgen_mcpwm_ensure_timer_inited(mcpwm_unit_t unit, mcpwm_timer_t timer, float duty_percent) {
    const int u = (int)unit;
    const int t = (int)timer;
    if (u < 0 || u >= 2 || t < 0 || t >= 3) return;
    if (s_stepgen_mcpwm_timer_inited[u][t]) return;

    mcpwm_config_t cfg = {};
    cfg.frequency = 1000; // placeholder; actual set by mcpwm_set_frequency
    cfg.cmpr_a = duty_percent;
    cfg.cmpr_b = duty_percent;
    cfg.counter_mode = MCPWM_UP_COUNTER;
    cfg.duty_mode = MCPWM_DUTY_MODE_0;
    (void)mcpwm_init(unit, timer, &cfg);
    (void)mcpwm_stop(unit, timer);
    s_stepgen_mcpwm_timer_inited[u][t] = true;
}

static void stepgen_mcpwm_apply_axis(stepgen_t* sg, uint8 axis, uint32 hz) {
    if (!sg) return;
    mcpwm_unit_t unit;
    mcpwm_timer_t timer;
    mcpwm_operator_t opr;
    mcpwm_io_signals_t sig;
    if (!stepgen_mcpwm_map_axis(axis, &unit, &timer, &opr, &sig)) {
        return;
    }

    const float duty = stepgen_step_duty_percent(&sg->cfg[axis]);
    stepgen_mcpwm_ensure_timer_inited(unit, timer, duty);

    if (hz == 0) {
        (void)mcpwm_stop(unit, timer);
        (void)mcpwm_set_signal_low(unit, timer, opr);
        return;
    }

    (void)mcpwm_set_frequency(unit, timer, hz);
    (void)mcpwm_set_duty(unit, timer, opr, duty);
    (void)mcpwm_set_duty_type(unit, timer, opr, MCPWM_DUTY_MODE_0);
    (void)mcpwm_start(unit, timer);
}
#endif

static void stepgen_apply_pwm(uint8 channel, uint8 resolution_bits, uint32 hz) {
    if (hz == 0) {
        ledcWrite(channel, 0);
        return;
    }

    // Setup channel frequency. Use 50% duty for continuous pulses.
    ledcSetup(channel, hz, resolution_bits);
    const uint32 maxDuty = (1UL << resolution_bits) - 1UL;
    ledcWrite(channel, maxDuty / 2);
}

boolean stepgen_init(stepgen_t* sg) {
    if (sg == NULL) {
        return false;
    }

    memset(sg, 0, sizeof(stepgen_t));

    // Defaults
    for (uint8 i = 0; i < STEPGEN_MAX_AXES; i++) {
        sg->cfg[i].enabled = false;
        sg->cfg[i].step_pin = 0xFF;
        sg->cfg[i].dir_pin = 0xFF;
        sg->cfg[i].invert_dir = false;
        sg->cfg[i].ledc_channel = i; // channels 0..3 by default
        sg->cfg[i].ledc_resolution_bits = 8;
        sg->cfg[i].step_time_us = 2;
        sg->cfg[i].step_space_us = 2;
        sg->cfg[i].dir_setup_us = 5;
        sg->cfg[i].dir_hold_us = 5;
        sg->cfg[i].steps_per_unit = 200.0f;
        sg->cfg[i].max_accel_units_s2 = 50.0f; // conservative default accel
        sg->cfg[i].max_step_hz = 200000;
        sg->cfg[i].pos_gain_hz_per_step = 10.0f;
        sg->cfg[i].pos_deadband_steps = 0;
        sg->mode[i] = STEPGEN_MODE_VELOCITY;
        sg->vel_cmd_hz[i] = 0;
        sg->pos_cmd_steps[i] = 0;
        sg->applied_vel_hz[i] = 0;
        sg->applied_hz_abs[i] = 0;
        sg->applied_dir[i] = 1;
        sg->dir_deadline_us[i] = 0;
    }

    sg->last_update_us = micros();
    sg->initialized = true;
    return true;
}

boolean stepgen_set_axis_config(stepgen_t* sg, uint8 axis, const stepgen_axis_config_t* cfg) {
    if (sg == NULL || cfg == NULL || !sg->initialized) {
        return false;
    }
    if (axis >= STEPGEN_MAX_AXES) {
        return false;
    }

    sg->cfg[axis] = *cfg;

    // Apply hardware pin config
    if (sg->cfg[axis].dir_pin != 0xFF) {
        pinMode(sg->cfg[axis].dir_pin, OUTPUT);
        digitalWrite(sg->cfg[axis].dir_pin, sg->cfg[axis].invert_dir ? HIGH : LOW);
    }
    if (sg->cfg[axis].step_pin != 0xFF) {
        pinMode(sg->cfg[axis].step_pin, OUTPUT);
#if STEPGEN_HAS_MCPWM
        mcpwm_unit_t unit;
        mcpwm_timer_t timer;
        mcpwm_operator_t opr;
        mcpwm_io_signals_t sig;
        if (stepgen_mcpwm_map_axis(axis, &unit, &timer, &opr, &sig)) {
            (void)mcpwm_gpio_init(unit, sig, (int)sg->cfg[axis].step_pin);
            const float duty = stepgen_step_duty_percent(&sg->cfg[axis]);
            stepgen_mcpwm_ensure_timer_inited(unit, timer, duty);
            (void)mcpwm_stop(unit, timer);
            (void)mcpwm_set_signal_low(unit, timer, opr);
        } else {
            ledcAttachPin(sg->cfg[axis].step_pin, sg->cfg[axis].ledc_channel);
            stepgen_apply_pwm(sg->cfg[axis].ledc_channel, sg->cfg[axis].ledc_resolution_bits, 0);
        }
#else
        ledcAttachPin(sg->cfg[axis].step_pin, sg->cfg[axis].ledc_channel);
        stepgen_apply_pwm(sg->cfg[axis].ledc_channel, sg->cfg[axis].ledc_resolution_bits, 0);
#endif
    }

    return true;
}

const stepgen_axis_config_t* stepgen_get_axis_config(stepgen_t* sg, uint8 axis) {
    if (sg == NULL || !sg->initialized) {
        return NULL;
    }
    if (axis >= STEPGEN_MAX_AXES) {
        return NULL;
    }
    return &sg->cfg[axis];
}

static void stepgen_update_positions(stepgen_t* sg, uint32 now_us) {
    uint32 dt_us = now_us - sg->last_update_us;
    sg->last_update_us = now_us;

    // pos_steps += applied_vel_hz * dt_us / 1e6
    // keep remainder in microseconds to reduce drift
    for (uint8 i = 0; i < STEPGEN_MAX_AXES; i++) {
        int32 hz = sg->applied_vel_hz[i];
        // accumulate signed micro-steps (hz * dt_us)
        int64 inc = (int64)hz * (int64)dt_us;
        int64 acc = (int64)sg->pos_rem_us[i] + inc;
        int64 steps = acc / 1000000LL;
        int32 rem = (int32)(acc % 1000000LL);
        sg->pos_steps[i] += steps;
        sg->pos_rem_us[i] = rem;
    }
}

static int32 stepgen_limit_accel_hz(stepgen_t* sg, uint8 axis, int32 target_hz, uint32 dt_us) {
    const stepgen_axis_config_t* c = &sg->cfg[axis];
    if (c->max_accel_units_s2 <= 0.0f || c->steps_per_unit <= 0.0f) {
        return target_hz;
    }

    // Convert units/s^2 to steps/s^2 (Hz/s), then to max delta Hz per dt
    const float accel_hz_s = c->max_accel_units_s2 * c->steps_per_unit; // steps/s^2
    const float dt_s = (float)dt_us / 1000000.0f;
    const float max_dhz_f = accel_hz_s * dt_s;
    int32 max_dhz = (max_dhz_f < 0.0f) ? 0 : (int32)(max_dhz_f + 0.5f);
    if (max_dhz <= 0) {
        return sg->applied_vel_hz[axis];
    }

    int32 cur = sg->applied_vel_hz[axis];
    int32 delta = target_hz - cur;
    if (delta > max_dhz) delta = max_dhz;
    if (delta < -max_dhz) delta = -max_dhz;
    return cur + delta;
}

static uint32 stepgen_effective_max_hz(const stepgen_axis_config_t* c) {
    uint32 max_hz = c->max_step_hz;
    const uint32 min_period_us = (uint32)c->step_time_us + (uint32)c->step_space_us;
    if (min_period_us > 0) {
        uint32 hw = 1000000UL / min_period_us;
        if (hw > 0) {
            max_hz = stepgen_min_u32(max_hz, hw);
        }
    }
    return max_hz;
}

void stepgen_process_pdo(stepgen_t* sg, const uint8* pdo_output, uint16 pdo_out_size, uint8* pdo_input, uint16 pdo_in_size) {
    if (sg == NULL || !sg->initialized) {
        return;
    }

    const uint32 now_us = micros();
    const uint32 dt_us = now_us - sg->last_update_us;
    stepgen_update_positions(sg, now_us);

    const uint16 base = 0; // start of PDO output (first 32 bytes reserved for stepgen in this minimal version)
    for (uint8 axis = 0; axis < STEPGEN_MAX_AXES; axis++) {
        const uint16 off = base + (uint16)axis * 8;
        if (off + 6 >= pdo_out_size) {
            break;
        }

        const boolean enable = pdo_output[off] ? true : false;
        const stepgen_mode_t mode = (stepgen_mode_t)(pdo_output[off + 1] & 0x01);
        int32 cmd = 0;
        memcpy(&cmd, &pdo_output[off + 2], 4);

        sg->mode[axis] = mode;
        if (mode == STEPGEN_MODE_POSITION) {
            sg->pos_cmd_steps[axis] = cmd;
        } else {
            sg->vel_cmd_hz[axis] = cmd;
        }

        int32 target_hz = 0;
        if (enable && sg->cfg[axis].enabled && sg->cfg[axis].step_pin != 0xFF) {
            if (mode == STEPGEN_MODE_POSITION) {
                // Position servo: v = err * Kp (Hz per step), then accel-limit.
                int64 err64 = (int64)sg->pos_cmd_steps[axis] - (int64)sg->pos_steps[axis];
                int64 abs_err = (err64 < 0) ? -err64 : err64;
                if ((uint32)abs_err <= sg->cfg[axis].pos_deadband_steps) {
                    target_hz = 0;
                } else {
                    const float kp = (sg->cfg[axis].pos_gain_hz_per_step > 0.0f) ? sg->cfg[axis].pos_gain_hz_per_step : 0.0f;
                    float vel_f = (float)err64 * kp;
                    if (vel_f > 2147483000.0f) vel_f = 2147483000.0f;
                    if (vel_f < -2147483000.0f) vel_f = -2147483000.0f;
                    target_hz = (int32)vel_f;
                }
            } else {
                target_hz = sg->vel_cmd_hz[axis];
            }

            // Clamp by max_step_hz and timing
            uint32 max_hz = stepgen_effective_max_hz(&sg->cfg[axis]);
            if (target_hz > (int32)max_hz) target_hz = (int32)max_hz;
            if (target_hz < -(int32)max_hz) target_hz = -(int32)max_hz;

            // Acceleration limiting
            target_hz = stepgen_limit_accel_hz(sg, axis, target_hz, dt_us);
        } else {
            target_hz = 0;
        }

        // Direction change handling with setup delay
        int8 desired_dir = (target_hz < 0) ? -1 : 1;
        if (target_hz == 0) {
            // Stop
            sg->applied_vel_hz[axis] = 0;
            sg->applied_hz_abs[axis] = 0;
#if STEPGEN_HAS_MCPWM
            stepgen_mcpwm_apply_axis(sg, axis, 0);
#else
            stepgen_apply_pwm(sg->cfg[axis].ledc_channel, sg->cfg[axis].ledc_resolution_bits, 0);
#endif
        } else {
            if (sg->applied_dir[axis] != desired_dir) {
                // Stop pulses, set dir, wait dir_setup_us
                sg->applied_vel_hz[axis] = 0;
                sg->applied_hz_abs[axis] = 0;
#if STEPGEN_HAS_MCPWM
                stepgen_mcpwm_apply_axis(sg, axis, 0);
#else
                stepgen_apply_pwm(sg->cfg[axis].ledc_channel, sg->cfg[axis].ledc_resolution_bits, 0);
#endif

                sg->applied_dir[axis] = desired_dir;
                if (sg->cfg[axis].dir_pin != 0xFF) {
                    boolean dir_level = (desired_dir > 0);
                    if (sg->cfg[axis].invert_dir) dir_level = !dir_level;
                    digitalWrite(sg->cfg[axis].dir_pin, dir_level ? HIGH : LOW);
                }
                sg->dir_deadline_us[axis] = now_us + (uint32)sg->cfg[axis].dir_setup_us;
            }

            if (sg->dir_deadline_us[axis] != 0 && (int32)(now_us - sg->dir_deadline_us[axis]) < 0) {
                // Still waiting
                sg->applied_vel_hz[axis] = 0;
                sg->applied_hz_abs[axis] = 0;
#if STEPGEN_HAS_MCPWM
                stepgen_mcpwm_apply_axis(sg, axis, 0);
#else
                stepgen_apply_pwm(sg->cfg[axis].ledc_channel, sg->cfg[axis].ledc_resolution_bits, 0);
#endif
            } else {
                sg->dir_deadline_us[axis] = 0;
                uint32 abs_hz = (target_hz < 0) ? (uint32)(-target_hz) : (uint32)target_hz;
                if (sg->applied_hz_abs[axis] != abs_hz) {
                    sg->applied_hz_abs[axis] = abs_hz;
#if STEPGEN_HAS_MCPWM
                    stepgen_mcpwm_apply_axis(sg, axis, abs_hz);
#else
                    stepgen_apply_pwm(sg->cfg[axis].ledc_channel, sg->cfg[axis].ledc_resolution_bits, abs_hz);
#endif
                }
                sg->applied_vel_hz[axis] = target_hz;
            }
        }

        // PDO input feedback: velocity + position
        if (pdo_input && pdo_in_size >= off + 8) {
            int32 fb_hz = sg->applied_vel_hz[axis];
            int32 fb_pos = (int32)sg->pos_steps[axis];
            memcpy(&pdo_input[off + 0], &fb_hz, 4);
            memcpy(&pdo_input[off + 4], &fb_pos, 4);
        }
    }
}

} // extern "C"

