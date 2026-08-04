---
id: C031
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

RULED OUT as the cause of the recompiled game's display-init failure: the guest seeing the runner's command line. Giving it a clean argv (image path moved to X2_IMAGE) changed nothing -- identical dialog, identical 4102-call block.

## Evidence

Rebuilt with the image path taken from the environment instead of argv[1], so the guest's GetCommandLineA no longer carries an extra token. Three frame samples at 55s/100s/140s: 592 colours, dominant black 44%, identical to the previous run; watchdog still reports 4102 recompiled calls then +0. Together with multiSampleType=0 having no effect, both hypotheses for the environmental difference from the stock run are now dead.

## What would falsify it

The actual difference between the stock run that rendered and this one remains UNIDENTIFIED. Next candidates, none tested: the guest image occupying 0x400000 while the runner's own sections sit above it may perturb what the engine's window/device code sees; the working directory or module filename the game derives paths from differs (it sees x2run.exe, not XMen2.exe); or DXVK behaves differently for a process whose main module is not the game.
