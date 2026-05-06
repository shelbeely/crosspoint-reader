#include "GitHubRateLimiter.h"

namespace github {

RateLimiter::RateLimiter(int capacity_, int refillPerSec)
    : capacity(capacity_),
      refillRate(refillPerSec),
      tokens(static_cast<double>(capacity_)),
      lastRefillEpoch(0),
      suspendedUntilEpoch(0) {}

void RateLimiter::refill(uint32_t nowEpochSec) {
  if (lastRefillEpoch == 0) {
    // First call seeds the clock without granting fake credit.
    lastRefillEpoch = nowEpochSec;
    return;
  }
  if (nowEpochSec <= lastRefillEpoch) return;
  const uint32_t elapsed = nowEpochSec - lastRefillEpoch;
  tokens += static_cast<double>(elapsed) * static_cast<double>(refillRate);
  if (tokens > static_cast<double>(capacity)) {
    tokens = static_cast<double>(capacity);
  }
  lastRefillEpoch = nowEpochSec;
}

bool RateLimiter::canRequest(uint32_t nowEpochSec) {
  if (isSuspended(nowEpochSec)) return false;
  refill(nowEpochSec);
  return tokens >= 1.0;
}

void RateLimiter::consume(uint32_t nowEpochSec) {
  refill(nowEpochSec);
  tokens -= 1.0;
  if (tokens < 0.0) tokens = 0.0;
}

void RateLimiter::suspendUntil(uint32_t resetEpoch) {
  if (resetEpoch > suspendedUntilEpoch) {
    suspendedUntilEpoch = resetEpoch;
  }
}

int RateLimiter::tokensAvailable(uint32_t nowEpochSec) {
  refill(nowEpochSec);
  return static_cast<int>(tokens);
}

}  // namespace github
