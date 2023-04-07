#ifndef SEEDSPEC6_H
#define SEEDSPEC6_H

#include <cstdint>

struct SeedSpec6
{
	uint8_t addr[16];
	uint16_t port;
};

#endif // SEEDSPEC6_H
