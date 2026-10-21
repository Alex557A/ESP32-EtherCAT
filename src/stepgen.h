/*
 * StepGen (STEP/DIR) for LinuxCNC over EtherCAT PDO
 *
 * "Adult" direction (but without overengineering yet):
 * - Supports POSITION and VELOCITY command modes (Mesa-like: position is preferred)
 * - Uses hardware PWM (LEDC) as a pragmatic first backend (continuous STEP train)
 * - Adds basic acceleration limiting and direction setup delay (dir_setup_us)
 *
 * NOTE:
 * - Next upgrade path: RMT/MCPWM backend for deterministic pulse width/spacing and lower jitter.
 */

#ifndef STEPGEN_H
#define STEPGEN_H

#include "ethercat_type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STEPGEN_MAX_AXES 4

typedef enum {
    STEPGEN_MODE_VELOCITY = 0, /**< cmd = velocity (Hz) */
    STEPGEN_MODE_POSITION = 1  /**< cmd = absolute position (steps) */
} stepgen_mode_t;

typedef struct {
    boolean enabled;
    uint8 step_pin;
    uint8 dir_pin;
    boolean invert_dir;

    // PWM backend (LEDC)
    uint8 ledc_channel;        // 0..15
    uint8 ledc_resolution_bits; // e.g. 8

    // Timing constraints (Mesa-like, microseconds)
    uint16 step_time_us;   /**< pulse width (future backend) */
    uint16 step_space_us;  /**< space between pulses (future backend) */
    uint16 dir_setup_us;   /**< time between DIR change and first STEP */
    uint16 dir_hold_us;    /**< time to hold DIR after last STEP (reserved) */

    // Motion parameters
    float steps_per_unit;      // steps per mm or per degree (for accel conversion)
    float max_accel_units_s2;  // units/s^2
    uint32 max_step_hz;        // hard limit

    // Position mode parameters (Mesa-like)
    float pos_gain_hz_per_step; /**< P-gain: Hz per step error */
    uint32 pos_deadband_steps;  /**< deadband around target position */
} stepgen_axis_config_t;

typedef struct {
    stepgen_axis_config_t cfg[STEPGEN_MAX_AXES];

    // Runtime command/state
    stepgen_mode_t mode[STEPGEN_MAX_AXES];
    int32 vel_cmd_hz[STEPGEN_MAX_AXES];        // signed Hz
    int32 pos_cmd_steps[STEPGEN_MAX_AXES];     // absolute steps (int32 for now)

    int32 applied_vel_hz[STEPGEN_MAX_AXES];    // signed Hz applied to PWM
    uint32 applied_hz_abs[STEPGEN_MAX_AXES];   // abs Hz (cached)
    int8 applied_dir[STEPGEN_MAX_AXES];        // -1/+1
    uint32 dir_deadline_us[STEPGEN_MAX_AXES];  // hold-off until time

    // Software-estimated position (steps)
    int64 pos_steps[STEPGEN_MAX_AXES];
    int32 pos_rem_us[STEPGEN_MAX_AXES];   // remainder accumulator

    uint32 last_update_us;
    boolean initialized;
} stepgen_t;

boolean stepgen_init(stepgen_t* sg);
boolean stepgen_set_axis_config(stepgen_t* sg, uint8 axis, const stepgen_axis_config_t* cfg);
const stepgen_axis_config_t* stepgen_get_axis_config(stepgen_t* sg, uint8 axis);

// PDO mapping (v2, compact 8 bytes per axis):
// For each axis i:
//   offset = base + i*8
//   OUT: [enable:1][mode:1][cmd:int32 LE][reserved:2]
//        mode=0 => cmd = vel_cmd_hz
//        mode=1 => cmd = pos_cmd_steps
//
//   IN:  [fb_vel_hz:int32 LE][fb_pos_steps:int32 LE]
void stepgen_process_pdo(stepgen_t* sg, const uint8* pdo_output, uint16 pdo_out_size, uint8* pdo_input, uint16 pdo_in_size);

#ifdef __cplusplus
}
#endif

#endif // STEPGEN_H

