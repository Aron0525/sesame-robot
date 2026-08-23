#pragma once

#include <Arduino.h>

enum ServoName : uint8_t {
  R1 = 0, 
  R2 = 1,
  L1 = 2,
  L2 = 3,
  R4 = 4,
  R3 = 5,
  L3 = 6,
  L4 = 7
};

const String ServoNames[]={"R1","R2","L1","L2","R4","R3","L3","L4"};

inline int servoNameToIndex(const String& servo) {
  if (servo == "L1") return L1;
  if (servo == "L2") return L2;
  if (servo == "L3") return L3;
  if (servo == "L4") return L4;
  if (servo == "R1") return R1;
  if (servo == "R2") return R2;
  if (servo == "R3") return R3;
  if (servo == "R4") return R4;
  return -1;
}

enum FaceAnimMode : uint8_t {
  FACE_ANIM_LOOP = 0,
  FACE_ANIM_ONCE = 1,
  FACE_ANIM_BOOMERANG = 2
};

// External globals and helpers used by movement/pose sequences
extern int frameDelay;
extern int walkCycles;
extern String currentCommand;

extern void setServoAngle(uint8_t channel, int angle);
extern void setFace(const String& faceName);
extern void setFaceMode(FaceAnimMode mode);
extern void setFaceWithMode(const String& faceName, FaceAnimMode mode);
extern void delayWithFace(unsigned long ms);
extern void enterIdle();
extern bool pressingCheck(String cmd, int ms);

// Pose/animation prototypes
void runRestPose();
void runStandPose(int face = 1);
void runWavePose();
void runDancePose();
void runProudPose();
void runSwimPose();
void runPushupPose();
void runCutePose();
void runFreakyPose();
void runWormPose();
void runShakePose();
void runShrugPose();
void runDeadPose();
void runCrabPose();
void runWalkPose();
void runWalkBackward();
void runTurnLeft();
void runTurnRight();

// ====== POSES ======
inline void runRestPose() { 
  Serial.println(F("REST")); 
  setFaceWithMode("rest", FACE_ANIM_BOOMERANG); 
  setServoAngle(R1, 90);
  setServoAngle(R2, 90);
  setServoAngle(L1, 90);
  setServoAngle(L2, 90);
  setServoAngle(R4, 90);
  setServoAngle(R3, 60);
  setServoAngle(L3, 90);
  setServoAngle(L4, 60);
}

inline void runStandPose(int face) { 
  Serial.println(F("STAND")); 
  if (face == 1) setFaceWithMode("stand", FACE_ANIM_ONCE); 
  setServoAngle(R1, 135); 
  setServoAngle(R2, 45); 
  setServoAngle(L1, 45); 
  setServoAngle(L2, 135);
  setServoAngle(R4, 38);
  setServoAngle(R3, 113);
  setServoAngle(L3, 41);
  setServoAngle(L4, 112);
  if (face == 1) enterIdle();
}

inline void runWavePose() { 
  Serial.println(F("WAVE")); 
  setFaceWithMode("wave", FACE_ANIM_ONCE); 
  runStandPose(0); 
  delayWithFace(200);
  setServoAngle(R4, 80); setServoAngle(L3, 180); 
  setServoAngle(L2, 90); setServoAngle(R1, 100); 
  delayWithFace(200);
  setServoAngle(L3, 180); 
  delayWithFace(300); 
  for (int i = 0; i < 4; i++) { 
    setServoAngle(L3, 180); delayWithFace(300); 
    setServoAngle(L3, 100); delayWithFace(300); 
  } 
  runStandPose(1); 
  if (currentCommand == "wave") currentCommand = "";
}

inline void runDancePose() {
  Serial.println(F("DANCE"));
  setFaceWithMode("dance", FACE_ANIM_LOOP);
  setServoAngle(R1, 90); setServoAngle(R2, 90);
  setServoAngle(L1, 90); setServoAngle(L2, 90);
  setServoAngle(R4, 145); setServoAngle(R3, 145);
  setServoAngle(L3, 25); setServoAngle(L4, 25);
  delayWithFace(300);
  for (int i = 0; i < 5; i++) {
    setServoAngle(R1, 90); setServoAngle(R2, 90);
    setServoAngle(L1, 90); setServoAngle(L2, 90);
    setServoAngle(R4, 130); setServoAngle(R3, 100);
    setServoAngle(L3, 25); setServoAngle(L4, 25);
    delayWithFace(300);
    setServoAngle(R1, 90); setServoAngle(R2, 90);
    setServoAngle(L1, 90); setServoAngle(L2, 90);
    setServoAngle(R4, 145); setServoAngle(R3, 145);
    setServoAngle(L3, 80); setServoAngle(L4, 80);
    delayWithFace(300); 
  } 
  runStandPose(1); 
  if (currentCommand == "dance") currentCommand = "";
}

inline void runProudPose() {
  Serial.println(F("PROUD"));
  setFaceWithMode("stand", FACE_ANIM_ONCE);

  // A: the upper legs stay at stand; lower legs move away from 90°.
  setServoAngle(R1, 135); setServoAngle(R2, 45);
  setServoAngle(L1, 45); setServoAngle(L2, 135);
  setServoAngle(R4, 23); setServoAngle(R3, 128);
  setServoAngle(L3, 26); setServoAngle(L4, 127);
  delayWithFace(300);

  for (int i = 0; i < 5; i++) {
    // B: cross the standing angle by 15° for the proud sway.
    setServoAngle(R1, 135); setServoAngle(R2, 45);
    setServoAngle(L1, 45); setServoAngle(L2, 135);
    setServoAngle(R4, 53); setServoAngle(R3, 98);
    setServoAngle(L3, 56); setServoAngle(L4, 97);
    delayWithFace(300);

    setServoAngle(R1, 135); setServoAngle(R2, 45);
    setServoAngle(L1, 45); setServoAngle(L2, 135);
    setServoAngle(R4, 23); setServoAngle(R3, 128);
    setServoAngle(L3, 26); setServoAngle(L4, 127);
    delayWithFace(300);
  }

  runStandPose(1);
  if (currentCommand == "proud") currentCommand = "";
}

inline void runSwimPose() {
  Serial.println(F("SWIM"));
  setFaceWithMode("swim", FACE_ANIM_ONCE);
  setServoAngle(R1, 90); setServoAngle(R2, 90);
  setServoAngle(L1, 90); setServoAngle(L2, 90);
  setServoAngle(R4, 90); setServoAngle(R3, 60);
  setServoAngle(L3, 90); setServoAngle(L4, 60);
  for (int i = 0; i < 4; i++) { 
    setServoAngle(R1, 135); setServoAngle(R2, 45); 
    setServoAngle(L1, 45); setServoAngle(L2, 135); 
    delayWithFace(400); 
    setServoAngle(R1, 90); setServoAngle(R2, 90); 
    setServoAngle(L1, 90); setServoAngle(L2, 90); 
    delayWithFace(400); 
  } 
  runStandPose(1); 
  if (currentCommand == "swim") currentCommand = "";
}

inline void runPushupPose() {
  Serial.println(F("PUSHUP"));
  setFaceWithMode("pushup", FACE_ANIM_ONCE);
  runStandPose(0); 
  delayWithFace(200);
  setServoAngle(L1, 0);
  setServoAngle(R1, 180);
  setServoAngle(L3, 90);
  setServoAngle(R3, 60);
  delayWithFace(500);
  for (int i = 0; i < 4; i++) {
    setServoAngle(L3, 0);
    setServoAngle(R3, 150);
    delayWithFace(600);
    setServoAngle(L3, 90);
    setServoAngle(R3, 60);
    delayWithFace(500);
  }
  runStandPose(1);
  if (currentCommand == "pushup") currentCommand = "";
}

inline void runCutePose() {
  Serial.println(F("CUTE"));
  setFaceWithMode("cute", FACE_ANIM_ONCE);
  runStandPose(0); 
  delayWithFace(200);
  setServoAngle(L2, 160);
  setServoAngle(R2, 20);
  setServoAngle(R4, 180);
  setServoAngle(L4, 0);

  setServoAngle(L1, 0);
  setServoAngle(R1, 180);
  setServoAngle(L3, 180);
  setServoAngle(R3, 0);
  delayWithFace(200);
  for (int i = 0; i < 5; i++) {
    setServoAngle(R4, 180);
    setServoAngle(L4, 45);
    delayWithFace(300);
    setServoAngle(R4, 135);
    setServoAngle(L4, 0);
    delayWithFace(300);
  }
  runStandPose(1);
  if (currentCommand == "cute") currentCommand = "";
}

inline void runFreakyPose() {
  Serial.println(F("FREAKY"));
  setFaceWithMode("freaky", FACE_ANIM_ONCE);
  runStandPose(0); 
  delayWithFace(200);
  setServoAngle(L1, 0);
  setServoAngle(R1, 180);
  setServoAngle(L2, 180);
  setServoAngle(R2, 0);
  setServoAngle(R4, 90);
  setServoAngle(R3, 0);
  delayWithFace(200);
  for (int i = 0; i < 3; i++) {
    setServoAngle(R3, 25);
    delayWithFace(400);
    setServoAngle(R3, 0);
    delayWithFace(400);
  }
  runStandPose(1);
  if (currentCommand == "freaky") currentCommand = "";
}

inline void runWormPose() {
  Serial.println(F("WORM"));
  setFaceWithMode("worm", FACE_ANIM_ONCE);
  runStandPose(0);
  delayWithFace(200);
  setServoAngle(R1, 180); setServoAngle(R2, 0); setServoAngle(L1, 0); setServoAngle(L2, 180);
  setServoAngle(R4, 90); setServoAngle(R3, 60); setServoAngle(L3, 90); setServoAngle(L4, 60);
  delayWithFace(200);
  for(int i=0; i<5; i++) {
    setServoAngle(R3, 15); setServoAngle(L3, 135); setServoAngle(R4, 45); setServoAngle(L4, 105);
    delayWithFace(300);
    setServoAngle(R3, 105); setServoAngle(L3, 45); setServoAngle(R4, 135); setServoAngle(L4, 15);
    delayWithFace(300);
  }
  runStandPose(1);
  if (currentCommand == "worm") currentCommand = "";
}

inline void runShakePose() {
  Serial.println(F("SHAKE"));
  setFaceWithMode("shake", FACE_ANIM_ONCE);
  runStandPose(0);
  delayWithFace(200);
  setServoAngle(R1, 135); setServoAngle(L1, 45); setServoAngle(L3, 90); setServoAngle(R3, 60);
  setServoAngle(L2, 90); setServoAngle(R2, 90);
  delayWithFace(200);
  for(int i=0; i<5; i++) {
    setServoAngle(R4, 45); setServoAngle(L4, 105);
    delayWithFace(300);
    setServoAngle(R4, 0); setServoAngle(L4, 150);
    delayWithFace(300);
  }
  runStandPose(1);
  if (currentCommand == "shake") currentCommand = "";
}

inline void runShrugPose() {
  Serial.println(F("SHRUG"));
  runStandPose(0);
  setFaceWithMode("dead", FACE_ANIM_ONCE);
  delayWithFace(200);
  setServoAngle(R3, 90); setServoAngle(R4, 90); setServoAngle(L3, 90); setServoAngle(L4, 90);
  delayWithFace(1000);
  setFaceWithMode("shrug", FACE_ANIM_ONCE);
  setServoAngle(R3, 0); setServoAngle(R4, 180); setServoAngle(L3, 180); setServoAngle(L4, 0);
  delayWithFace(1500);
  runStandPose(1);
  if (currentCommand == "shrug") currentCommand = "";
}

inline void runDeadPose() {
  Serial.println(F("DEAD"));
  runStandPose(0);
  setFaceWithMode("dead", FACE_ANIM_BOOMERANG);
  delayWithFace(200);
  setServoAngle(R3, 60); setServoAngle(R4, 90); setServoAngle(L3, 90); setServoAngle(L4, 60);
  if (currentCommand == "dead") currentCommand = "";
}

inline void runCrabPose() {
  Serial.println(F("CRAB"));
  setFaceWithMode("crab", FACE_ANIM_ONCE);
  runStandPose(0);
  delayWithFace(200);
  setServoAngle(R1, 90); setServoAngle(R2, 90); setServoAngle(L1, 90); setServoAngle(L2, 90);
  setServoAngle(R4, 0); setServoAngle(R3, 150); setServoAngle(L3, 45); setServoAngle(L4, 105);
  for(int i=0; i<5; i++) {
    setServoAngle(R4, 45); setServoAngle(R3, 105); setServoAngle(L3, 0); setServoAngle(L4, 150);
    delayWithFace(300);
    setServoAngle(R4, 0); setServoAngle(R3, 150); setServoAngle(L3, 45); setServoAngle(L4, 105);
    delayWithFace(300);
  }
  runStandPose(1);
  if (currentCommand == "crab") currentCommand = "";
}

// --- MOVEMENT ANIMATIONS ---
// All gait servos use a 45-degree range. Absolute endpoints differ because the
// left/right joints are mounted with different mechanical zero positions.
inline void runWalkPose() {
  Serial.println(F("WALK FWD"));
  setFaceWithMode("walk", FACE_ANIM_ONCE);
  // Initial Step
  setServoAngle(R3, 98); setServoAngle(L3, 56);
  setServoAngle(R2, 90); setServoAngle(L1, 45);
  if (!pressingCheck("forward", frameDelay)) return;
  
  for (int i = 0; i < walkCycles; i++) {
    // Mirror the two left legs onto the right legs without changing endpoints.
    // R3 is reissued at the boundary to keep its low position through 0 ms.
    setServoAngle(R3, 98); setServoAngle(L3, 11); setServoAngle(R4, 8);
    if (!pressingCheck("forward", 80)) return;
    setServoAngle(L4, 97); setServoAngle(L2, 90);
    if (!pressingCheck("forward", 20)) return;
    setServoAngle(R1, 135);
    if (!pressingCheck("forward", frameDelay)) return;
    setServoAngle(R2, 45); setServoAngle(L1, 90); setServoAngle(R4, 53);
    if (!pressingCheck("forward", 80)) return;
    setServoAngle(L4, 142);
    if (!pressingCheck("forward", 20)) return;
    setServoAngle(R3, 143);
    if (!pressingCheck("forward", frameDelay)) return;
    setServoAngle(L3, 56); setServoAngle(R2, 90); setServoAngle(L1, 45);
    if (!pressingCheck("forward", 80)) return;
    setServoAngle(L2, 135);
    if (!pressingCheck("forward", 20)) return;
    setServoAngle(R1, 90); setServoAngle(R3, 98);
    if (!pressingCheck("forward", 80)) return;
  }
  runStandPose(1);
}

// Logic reversed from Walk
inline void runWalkBackward() {
  Serial.println(F("WALK BACK"));
  setFaceWithMode("walk", FACE_ANIM_ONCE);
  if (!pressingCheck("backward", frameDelay)) return;
  
  for (int i = 0; i < walkCycles; i++) {
    setServoAngle(R3, 98); setServoAngle(L3, 11);
    if (!pressingCheck("backward", frameDelay)) return;
    setServoAngle(L4, 97); setServoAngle(L2, 135);
    setServoAngle(R4, 8); setServoAngle(R1, 90);
    if (!pressingCheck("backward", frameDelay)) return;    
    setServoAngle(R2, 90); setServoAngle(L1, 45);
    if (!pressingCheck("backward", frameDelay)) return;
    setServoAngle(R4, 53); setServoAngle(L4, 142);
    if (!pressingCheck("backward", frameDelay)) return;
    setServoAngle(R3, 143); setServoAngle(L3, 56);
    setServoAngle(R2, 45); setServoAngle(L1, 90);
    if (!pressingCheck("backward", frameDelay)) return;  
    setServoAngle(L2, 90); setServoAngle(R1, 135);
    if (!pressingCheck("backward", frameDelay)) return;
  }
  runStandPose(1);
}

// Simple turn logic
inline void runTurnLeft() {
  Serial.println(F("TURN LEFT"));
  setFaceWithMode("walk", FACE_ANIM_ONCE);
  for (int i = 0; i < walkCycles; i++) {
    //legset 1 (R1 L2)
    setServoAngle(R3, 98); setServoAngle(L4, 97);
    if (!pressingCheck("left", frameDelay)) return;
    setServoAngle(R1, 135); setServoAngle(L2, 135);
    if (!pressingCheck("left", frameDelay)) return;
    setServoAngle(R3, 143); setServoAngle(L4, 142);
    if (!pressingCheck("left", frameDelay)) return;
    setServoAngle(R1, 90); setServoAngle(L2, 90);
    if (!pressingCheck("left", frameDelay)) return;
      //legset 2 (R2 L1)
    setServoAngle(R4, 53); setServoAngle(L3, 56);
    if (!pressingCheck("left", frameDelay)) return;
    setServoAngle(R2, 90); setServoAngle(L1, 90); 
    if (!pressingCheck("left", frameDelay)) return;
    setServoAngle(R4, 8); setServoAngle(L3, 11);
    if (!pressingCheck("left", frameDelay)) return;
    setServoAngle(R2, 45); setServoAngle(L1, 45);
    if (!pressingCheck("left", frameDelay)) return;  
  }
  runStandPose(1);
}

inline void runTurnRight() {
  Serial.println(F("TURN RIGHT"));
  setFaceWithMode("walk", FACE_ANIM_ONCE);
  for (int i = 0; i < walkCycles; i++) {
    //legset 2 (R2 L1)
    setServoAngle(R4, 53); setServoAngle(L3, 56);
    if (!pressingCheck("right", frameDelay)) return;
    setServoAngle(R2, 45); setServoAngle(L1, 45);
    if (!pressingCheck("right", frameDelay)) return;
    setServoAngle(R4, 8); setServoAngle(L3, 11);
    if (!pressingCheck("right", frameDelay)) return;
    setServoAngle(R2, 90); setServoAngle(L1, 90);
    if (!pressingCheck("right", frameDelay)) return;  
    //legset 1 (R1 L2)
    setServoAngle(R3, 98); setServoAngle(L4, 97);
    if (!pressingCheck("right", frameDelay)) return;
    setServoAngle(R1, 90); setServoAngle(L2, 90); 
    if (!pressingCheck("right", frameDelay)) return;
    setServoAngle(R3, 143); setServoAngle(L4, 142);
    if (!pressingCheck("right", frameDelay)) return;
    setServoAngle(R1, 135); setServoAngle(L2, 135);
    if (!pressingCheck("right", frameDelay)) return;
  }
  runStandPose(1);
}
