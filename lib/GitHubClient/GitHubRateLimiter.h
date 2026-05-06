// Token-bucket rate limiter for `lib/GitHubClient`. Pure C++ so it can
// be host-tested. Holds no clock of its own — the caller passes the
// current epoch time to every call so we can mock it in tests.
//
// GitHub REST allows 5000 requests/hour per token. We don't try to
// model that exactly; instead we throttle to roughly 1 request/second
// average with a small burst budget, which keeps companion-mode polls
// well under the limit even with the user mashing Refresh.
//
// On a 403/429 response with `X-RateLimit-Reset` set, we suspend
// further requests until that epoch passes. This is the only authority
// — we trust GitHub's reset time over our local bucket.

#pragma once

#include <cstddef>
#include <cstdint>

namespace github {

class RateLimiter {
 public:
  // Bucket capacity (max burst) and refill rate (tokens per second).
  // Defaults chosen so a normal companion-home open (≤7 requests) drains
  // the bucket but recovers in ≤7 seconds.
  static constexpr int DEFAULT_CAPACITY = 8;
  static constexpr int DEFAULT_REFILL_PER_SEC = 1;

  RateLimiter(int capacity = DEFAULT_CAPACITY, int refillPerSec = DEFAULT_REFILL_PER_SEC);

  // True if the caller may issue a request now. Must be paired with
  // `consume(now)` if the caller actually proceeds. Idempotent — does
  // not consume tokens.
  bool canRequest(uint32_t nowEpochSec);

  // Consumes one token. Caller is responsible for verifying
  // `canRequest()` first; consume() simply decrements (with a floor of
  // zero) and refills based on elapsed time.
  void consume(uint32_t nowEpochSec);

  // Suspend until at least `resetEpoch`. Called with the value of the
  // `X-RateLimit-Reset` header on a 403/429 response, which is the
  // authoritative recovery time per GitHub's docs.
  void suspendUntil(uint32_t resetEpoch);

  // Inspectors — useful for the planned status-bar badge and tests.
  int tokensAvailable(uint32_t nowEpochSec);
  uint32_t suspendedUntil() const { return suspendedUntilEpoch; }
  bool isSuspended(uint32_t nowEpochSec) const { return nowEpochSec < suspendedUntilEpoch; }

 private:
  void refill(uint32_t nowEpochSec);

  int capacity;
  int refillRate;             // tokens/sec
  double tokens;              // double for fractional accumulation
  uint32_t lastRefillEpoch;
  uint32_t suspendedUntilEpoch;
};

}  // namespace github
