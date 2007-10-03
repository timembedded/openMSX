// $Id$

#ifndef CLOCK_HH
#define CLOCK_HH

#include "EmuDuration.hh"
#include "EmuTime.hh"
#include "Math.hh"
#include "static_assert.hh"
#include <cassert>

namespace openmsx {

/** Represents a clock with a fixed frequency.
  * The frequency is in Hertz, so every tick is 1/frequency second.
  * A clock has a current time, which can be increased by
  * an integer number of ticks.
  */
template <unsigned FREQ_NOM, unsigned FREQ_DENOM = 1>
class Clock
{
private:
	// stuff below calculates:
	//   MASTER_TICKS = MAIN_FREQ / (FREQ_NOM / FREQ_DENOM) + 0.5
	// intermediate results are 96bit, that's why it's a bit complicated
	static const unsigned long long P =
		(MAIN_FREQ & 0xFFFFFFFF) * FREQ_DENOM + (FREQ_NOM / 2);
	static const unsigned long long Q =
		(MAIN_FREQ >> 32) * FREQ_DENOM + (P >> 32);
	static const unsigned long long R1 = Q / FREQ_NOM;
	static const unsigned long long R0 =
		(((Q - (R1 * FREQ_NOM)) << 32) + (P & 0xFFFFFFFF)) / FREQ_NOM;
	static const unsigned long long MASTER_TICKS = (R1 << 32) + R0;
	static const unsigned MASTER_TICKS32 = MASTER_TICKS;
	STATIC_ASSERT(MASTER_TICKS < 0x100000000ull);

public:
	// Note: default copy constructor and assigment operator are ok.

	/** Calculates the duration of the given number of ticks at this
	  * clock's frequency.
	  */
	static const EmuDuration duration(unsigned ticks) {
		return EmuDuration(ticks * MASTER_TICKS);
	}

	/** Create a new clock, which starts ticking at the given time.
	  */
	explicit Clock(const EmuTime& e)
		: lastTick(e) { }

	/** Gets the time at which the last clock tick occurred.
	  */
	const EmuTime& getTime() const { return lastTick; }

	/** Checks whether this clock's last tick is or is not before the
	  * given time stamp.
	  */
	bool before(const EmuTime& e) const {
		return lastTick.time < e.time;
	}

	/** Calculate the number of ticks for this clock until the given time.
	  * It is not allowed to call this method for a time in the past.
	  */
	unsigned getTicksTill(const EmuTime& e) const {
		assert(e.time >= lastTick.time);
		return Math::div_64_32(e.time - lastTick.time, MASTER_TICKS32);
	}

	/** Calculate the time at which this clock will have ticked the given
	  * number of times (counted from its last tick).
	  */
	const EmuTime operator+(uint64 n) const {
		return EmuTime(lastTick.time + n * MASTER_TICKS);
	}

	/** Reset the clock to start ticking at the given time.
	  */
	void reset(const EmuTime& e) {
		lastTick.time = e.time;
	}

	/** Advance this clock in time until the last tick which is not past
	  * the given time.
	  * It is not allowed to advance a clock to a time in the past.
	  */
	void advance(const EmuTime& e) {
		assert(lastTick.time <= e.time);
		lastTick.time = e.time -
			Math::mod_64_32(e.time - lastTick.time, MASTER_TICKS32);
	}

	/** Advance this clock by the given number of ticks.
	  */
	void operator+=(unsigned n) {
		lastTick.time += n * MASTER_TICKS;
	}

	/** Advance this clock by the given number of ticks.
	  * This method is similar to operator+=, but it's optimized for
	  * speed. OTOH the amount of ticks should not be too large,
	  * otherwise an overflow occurs. Use operator+() when the duration
	  * of the ticks approaches 1 second.
	  */
	void fastAdd(unsigned n) {
		#ifdef DEBUG
		// we don't even want this overhead in development versions
		assert((n * MASTER_TICKS) < (1ull << 32));
		#endif
		lastTick.time += n * MASTER_TICKS32;
	}

private:
	/** Time of this clock's last tick.
	  */
	EmuTime lastTick;
};

} // namespace openmsx

#endif
