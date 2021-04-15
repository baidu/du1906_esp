# 2020-06-08 Audio IDF
- Audio IDF has been released based on release v3.3.1 (143d26aa49df524e10fb8e41a71d12e731b9b71d)
- Please check the patchs from `idf_patchs_refer` folder before use.
- Change commit message
```c
a19fd7b84d components/bt: Fix assert when controller received an HCI but did not want it
ea300e535d Fixed rare bug in the ble mesh run outof btc queue. Change the task_post abort to abort_with_coredump.
f0f4c7cddd components/bt: assert in host, to coredump param
3cfdd85294 Fix live lock in bt isr immediately
3a55fd9aaa Fix live lock int bt isr using cod multicore debug
ecd39e77bc Fix for mi6 compatibility
```
- WIFI-BT :
    - components/bt: Fix assert when controller received an HCI but did not want it
    - Fixed rare bug in the ble mesh run outof btc queue. Change the task_  - to abort_with_coredump.
    - components/bt: assert in host, to coredump param
    - Fix live lock in bt isr immediately
    - Fix live lock int bt isr using cod multicore debug
    -   -Fix for mi6 compatibility

# 2020-06-01 Audio IDF
- Audio IDF has been released based on release v3.3.1 (143d26aa49df524e10fb8e41a71d12e731b9b71d)
- Please check the patchs from `idf_patchs_refer` folder before use.
- Change commit message
```c
e45281d8a4 components/bt: Disable exception mode after saving special registers
5cefdc6b5e Revert "fix live lock in bt isr immediately"
```
- WIFI-BT :
    - components/bt: Disable exception mode after saving special registers
    - Revert "fix live lock in bt isr immediately"