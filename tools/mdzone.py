#!/usr/bin/env python3
"""mdzone.py <zone> [frames]: tools/mdhost.c inputs that enter Sonic the Hedgehog's level
select at the title (Up, Down, Left, Right, then A + Start), pick entry <zone> (0-2 Green
Hill 1-3, 3-5 Labyrinth, 6-8 Marble, 9-11 Star Light, 12-14 Spring Yard, 15-17 Scrap
Brain, 18 Final Zone, 19 Special Stage), then run right and jump for [frames] frames."""
import sys
z = int(sys.argv[1]); play = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
seq = []; f = 400
for p in ('fe', 'fd', 'fb', 'f7'): seq += [(f, p), (f + 4, 'ff')]; f += 10
seq += [(f, 'bf'), (f + 4, '3f'), (f + 10, 'ff')]; f += 60
for i in range(z): seq += [(f, 'fd'), (f + 4, 'ff')]; f += 10
seq += [(f, '7f'), (f + 5, 'ff')]; f += 200
while f < 460 + play:
    seq += [(f, 'f7'), (f + 50, 'b7'), (f + 62, 'f7')]; f += 90
print(' '.join('-i %d:%s' % x for x in seq[:250]))
