// SPDX-License-Identifier: GPL-2.0-or-later
// Locomotion in Play (task 0127): Walk Cycle's gait made frame by frame from how fast and which way the character is
// going - WASD (or the arrows) with this component's own keys, a PlayerController or a Rigidbody moving the object, or a
// script's set_move - so the legs step wherever it goes: the phase advancing with the speed, walk blending into run,
// each swinging foot aimed at where it will land and put on the ground there (a ramp, stairs), the pelvis kept within
// reach of both feet, the body leaning into turns. Nothing is keyed: it poses the rig live, after the Animation and
// Sequencer components and before IK and the spring bones. Off, an Animation clip owns the upper body (Upper Body off)
// or the whole rig (the component off).
#pragma once
#include "../core/math.h"
#include "gait.h"
#include "raycast.h"
#include "scene.h"
#include <string>
#include <vector>

namespace bl {

struct Locomotion : ComponentBase<Locomotion> {
  static constexpr const char *kName = "Locomotion";
  std::string walk = "walk", run = "run";  // the styles at walking and running speed (blended between by speed)
  float walk_speed = 1.4f;   // m/s: W
  float run_speed = 3.5f;    // m/s: Shift+W
  float acceleration = 4.0f; // m/s a second: starting and stopping
  float turn_speed = 180.0f; // degrees a second: A and D, or turning to a script's direction
  bool use_keys = true;      // WASD and the arrows move the object (a PlayerController on it moves it instead)
  bool upper_body = true;    // the spine, head and arms too (off: left to an Animation clip)
  bool follow_ground = true; // the object kept on the ground under it
  static inline bool live = false;  // the Locomotion assistant on (the editor sets it): off, the component does nothing

  /* A script's wish: `velocity` in metres a second (world, along the ground); the object turns to face it. */
  void set_move(Vec3 velocity) { wanted_ = velocity, scripted_ = true; }

  void reflect(Reflector &r) override;
  void start(PlayContext &ctx) override;
  void update(PlayContext &ctx) override;

  /* ---- Play state (not saved) ---- */
  struct Leg {
    Vec3 plant, from, to;   // where the ankle rests (down), where a swing started and is going
    Quat plant_turn;        // the body's turn when the foot landed (a planted foot doesn't turn with the body)
    bool down = true;
  };
  bool ready_ = false;
  gait::Rig rig_;
  Mat4 start_world_;                 // the object's world matrix when the rig was read
  Quat start_turn_;                  // and its turn
  std::vector<Leg> legs_;
  struct Rest {
    uint64_t id = 0;     // the bone or IK control (by id: a script may destroy objects in Play)
    Transform local;     // as Play started (each frame starts from it)
    bool upper = false;  // the spine, head or an arm's (left to a clip with Upper Body off)
  };
  std::vector<Rest> rest_;
  std::vector<uint64_t> rig_ids_;    // every object rig_ points at: one gone, and the rig is read again
  std::vector<Vec3> pole_rest_;      // the legs' IK poles as Play started (world)
  float phase_ = 0.0f;               // cycles walked
  float speed_ = 0.0f;               // m/s along the ground, as the object moves
  Vec3 last_pos_;
  Quat last_turn_;
  float yaw_rate_ = 0.0f;            // degrees a second the object is turning (the lean into turns)
  Vec3 wanted_;
  bool scripted_ = false;
  float cost_ms_ = 0.0f;             // the last step's time (the tests' budget)
};

namespace locomotion {

/* Every Locomotion component's rig posed for this frame (`dt` seconds since the last), its feet on `ground`. Called by
 * the Play loop after the scene's components update (the object moved) and before IK and the springs; switched off with
 * the Locomotion assistant. */
void step(Scene &scene, float dt, const GroundGrid &ground);

/* The ground grid Play builds once at start: every rendered mesh but the characters with Locomotion. */
GroundGrid ground_for(const Scene &scene);

}  // namespace locomotion
}  // namespace bl
