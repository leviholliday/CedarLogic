#include "logic_event.h"

TimeType Event::getCreationTime() const {
	return myCreationTime;
};

bool Event::operator > (const Event &other) const {

	if (eventTime == other.eventTime) {
		return (getCreationTime() > other.getCreationTime());
	}

	return (eventTime > other.eventTime);
}

std::atomic<TimeType> Event::globalCreationTime{0};
