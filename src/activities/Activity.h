#pragma once
#include <Logging.h>

#include <cassert>
#include <memory>
#include <string>
#include <utility>

#include "ActivityManager.h"  // for using the ActivityManager singleton
#include "ActivityResult.h"
#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "RenderLock.h"
#include "util/ScreenshotInfo.h"

/**
 * @brief Base class for every screen in the firmware.
 *
 * An Activity is a full-screen controller. The pattern mirrors Android's Activity:
 * one screen is active at a time, managed on a stack by ActivityManager.
 *
 * Lifecycle (called by ActivityManager):
 *   onEnter()  — allocate resources, open files, start FreeRTOS tasks, call requestUpdate()
 *   loop()     — called every main-loop iteration; handle input, update state
 *   render()   — called from the render task when requestUpdate() was triggered; issue draw calls
 *   onExit()   — free all resources in reverse order of allocation; delete tasks before objects they use
 *
 * Memory rules:
 *   - Allocate long-lived buffers on the heap in onEnter(), free in onExit()
 *   - Keep stack locals under 256 bytes; larger data goes on the heap
 *   - Do not allocate or free in the render() hot path
 *   - FreeRTOS tasks must be deleted in onExit() before any objects they reference are destroyed
 *   - File handles (HalFile / FsFile) must be closed in onExit()
 *
 * The activity is heap-allocated and deleted by ActivityManager after onExit() returns.
 */
class Activity {
  friend class ActivityManager;

 protected:
  std::string name;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

  ActivityResultHandler resultHandler;
  ActivityResult result;

 public:
  explicit Activity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : name(std::move(name)), renderer(renderer), mappedInput(mappedInput) {}
  virtual ~Activity() = default;
  virtual void onEnter();
  virtual void onExit();
  virtual void loop() {}

  virtual void render(RenderLock&&) {}

  // If immediate is true, the update will be triggered immediately.
  // Otherwise, it will be deferred until the end of the current loop iteration.
  virtual void requestUpdate(bool immediate = false);

  // Request an immediate render and block until it completes.
  virtual RequestUpdateResult requestUpdateAndWait();

  virtual bool skipLoopDelay() { return false; }
  virtual bool preventAutoSleep() { return false; }
  virtual bool isReaderActivity() const { return false; }
  virtual bool allowPowerAsConfirmInReaderMode() const { return false; }
  virtual bool canSnapshotForSleepOverlay() const { return false; }
  virtual ScreenshotInfo getScreenshotInfo() const { return {}; }

  // Start a new activity without destroying the current one
  // Note: requestUpdate() will be invoked automatically once resultHandler finishes
  void startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler);

  // Set the result to be passed back to the previous activity when this activity finishes
  void setResult(ActivityResult&& result);

  // Finish this activity and return to the previous one on the stack (if any)
  void finish();

  // Convenience method to facilitate API transition to ActivityManager
  // TODO: remove this in near future
  void onGoHome();
  void onSelectBook(const std::string& path);
};
