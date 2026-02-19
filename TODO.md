# OpenQ4 TODO

This file tracks current known issues and upcoming features.

## Known Issues

- [ ] Viewport sometimes remains black when changing between SP and MP; investigate update/refresh logic during module transitions.
- [ ] Menu cursor handling needs improvement (focus, capture, and consistency across input modes and resolutions).
- [ ] The locked door bug inherited from Quake4Doom still persists and needs a root-cause fix.
- [ ] Machinegun zoom projection yaw differs from viewangles yaw.

## Upcoming Features and Improvements

- [ ] Add an optional shadow mapping setting/path.
- [ ] Review and port relevant features and improvements implemented in the last 5 years of RBDOOM3-BFG commits.
- [ ] Merge shared code between MP/SP and streamline the process of switching between each.
- [ ] Find and implement ways to improve loading times.
- [ ] Expand multiplayer controls and port relevant WORR functionality/logic.
- [ ] Improve menu and loading screen horizontal expansion behavior.
- [ ] Machinegun and railgun zoom images need to suit all screen aspect ratios.
- [ ] CPMA-esque rainbow a-z color escapes implementation, disable black

## Potential change in project scope

Project may benefit from catering towards multiple idTech4 titles, to include: Doom 3, Doom 3: BFG, Prey, ETQW