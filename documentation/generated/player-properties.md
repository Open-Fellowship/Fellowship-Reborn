### Health

| property | key | type | default | |
|---|---|---|---|---|
| Initial Health | `InitialHealth` | float | 100.0 |  |
| Max Health | `MaxHealth` | float | 100.0 |  |
| Difficulty Modifier Object | `Difficulty` | object reference (modifier 2) | - | -> Difficulty Modifier |
| Invulnerable? | `Invulnerable` | enum | 0 | `No` `Yes` |
| Invulnerability ON Message | `InvulnerablilityOnMsg` | message | 90 |  |
| Invulnerability Off Message | `InvulnerablilityOffMsg` | message | 91 |  |
| Danger Rating (0.0 - 1.0) | `DangerRating` | float | 0.5 |  |

### Melee Attack Positions

| property | key | type | default | |
|---|---|---|---|---|
| Number of Melee Attack Positions Around Me | `NumMeleeAttackPositions` | int | 4 |  |
| Melee Attack Positions Radius (lu) | `MeleeAttackPositionsRadius` | float | 0.4 |  |

### Incoming Messages

| property | key | type | default | |
|---|---|---|---|---|
| Message To Send To Blow Up | `MsgBlowUp` | message | 11 |  |

### Health Regeneration

| property | key | type | default | |
|---|---|---|---|---|
| Heal Speed (health points / sec) | `HealSpeed` | float | 0.75 |  |
| Critical Health Percentage ( 0 - 100) | `CriticalHealthPerc` | float | 20.0 |  |

### Hit Reactions

| property | key | type | default | |
|---|---|---|---|---|
| Hit Reactions | `HitReactions` | object reference (modifier 3) | - | -> Hit Reaction |
| Special Hit Reactions | `SpecialHitReactions` | object reference (modifier 3) | - | -> Hit Reaction |
| Horizontal Speed While Falling (wu/sec) | `HitReactionFallXZSpeed` | float | 3000.0 |  |

### Death

| property | key | type | default | |
|---|---|---|---|---|
| Death anim list | `DeathAnims` | list of animation | - |  |
| Timer to Activate Reset Option (sec) | `TimeUntilReset` | float | 2.0 |  |

### Basic Movement

| property | key | type | default | |
|---|---|---|---|---|
| Start Sneak/Walk Blend (0-1) | `StartSneakWalk` | float | 0.2 |  |
| End Sneak/Walk Blend (0-1) | `EndSneakWalk` | float | 0.3 |  |
| Start Walk/Run Blend (0-1) | `StartWalkRun` | float | 0.5 |  |
| End Walk/Run Blend (0-1) | `EndWalkRun` | float | 0.9 |  |
| First Person Strafe Speed (wu/s) | `StrafeSpeed` | float | 10000.0 |  |
| Rotation Speed (degree/s) | `RotationSpeed` | float | 1000.0 |  |
| Minimun Rotation to Unlock Movement (degrees) | `UnlockAngle` | float | 45.0 |  |
| Footstep Set To Use | `FootStepSet` | enum | 0 | `Player 1` `Player 2` `Light NPC` `Heavy NPC` |

### Basic Movement Animations

| property | key | type | default | |
|---|---|---|---|---|
| Ready | `ReadyAnim` | animation | - |  |
| Sneak | `SneakAnim` | animation | - |  |
| Walk | `WalkAnim` | animation | - |  |
| Run | `RunAnim` | animation | - |  |

### Idle State Parameters

| property | key | type | default | |
|---|---|---|---|---|
| Idle Anim list | `IdleAnims` | list of animation | - |  |
| Time to first Idle (sec) | `TimeToFirstIdle` | float | 5.0 |  |
| Time to Subsequent Idle (sec) | `TimeToNextIdle` | float | 30.0 |  |
| First Idle time deviation (sec) | `FirstIdleTimeDeviation` | float | 1.0 |  |
| Subsequent Idle time deviation (sec) | `NextIdleTimeDeviation` | float | 10.0 |  |

### Jump/Fall

| property | key | type | default | |
|---|---|---|---|---|
| Vertical Jump Velocity (wu/s) | `JumpVerVel` | float | 12000.0 |  |
| Horizontal Jump Velocity (wu/s) | `JumpHorVel` | float | 10000.0 |  |
| In air horizontal acceleration (wu/s2) | `JumphorAccel` | float | 100000.0 |  |
| Falling Horizontal Velocity Damping | `FallDamping` | float | 0.8 |  |
| Minimum Fall Damage Speed (wu/s) | `MinFallDamageSpeed` | float | 17000.0 |  |
| Maximum Fall Damage Speed (wu/s) | `MaxFallDamageSpeed` | float | 35000.0 |  |
| Minimum Speed to Play Landing Anim (wu/s) | `MinLandingYSpeed` | float | 7000.0 |  |

### Jump Related Animations

| property | key | type | default | |
|---|---|---|---|---|
| Jump | `JumpAnim` | animation | - |  |
| Fall | `FallAnim` | animation | - |  |
| Landing | `JumpLanding` | animation | - |  |

### Ladder Climbing

| property | key | type | default | |
|---|---|---|---|---|
| Ladder Climb Cycle | `LadderClimbCycle` | animation | - |  |
| Ladder Dismount Bottom | `LadderDismountBottom` | animation | - |  |
| Ladder Dismount Top Left | `LadderDismountTopL` | animation | - |  |
| Ladder Dismount Top Right | `LadderDismountTopR` | animation | - |  |
| Ladder Idle Animation | `LadderIdleAnim` | animation | - |  |

### Ledge Grabbing

| property | key | type | default | |
|---|---|---|---|---|
| Hang Height Offset (wu) | `HangHeightOffset` | float | 1230.0 |  |
| Distance To Ledge During Hang (wu) | `HangDistance` | float | 280.0 |  |
| Ledge Hang | `HangAnim` | animation | - |  |
| Ledge Hoist | `HoistAnim` | animation | - |  |
| Shimmy Left | `ShimmyLAnim` | animation | - |  |
| Shimmy Right | `ShimmyRAnim` | animation | - |  |

### Object Interaction

| property | key | type | default | |
|---|---|---|---|---|
| Use Switch Anim | `UseSwitchAnim` | animation | - |  |

### Collision Detection

| property | key | type | default | |
|---|---|---|---|---|
| Collider Object | `Collider` | object reference (modifier 2) | - | -> Collider |
| Step Height (if higher than this, start falling (wu)) | `StepHeight` | float | 600.0 |  |

### Item Interaction

| property | key | type | default | |
|---|---|---|---|---|
| Item Auto-Pickup XZ Projection Distance (lu) | `ItemAutoPickupDist` | float | 0.5 |  |
| Item Auto-Pickup Maximum Y Offset (lu) | `ItemAutoPickupYOffset` | float | 2.0 |  |
| Intial Inventory Items | `InitialInventory` | object reference (modifier 5) | - | -> 0xa |
| Unarmed Weapons | `UnarmedWeapons` | object reference (modifier 3) | - | -> Melee Weapon |
| Attachments | `Attachments` | list of object reference | - | -> 0x0 |
| Attachment Channels | `AttachmentChannels` | list of channel | none |  |
| Inventory | `InventoryGUI` | object reference (modifier 2) | - | -> GUI Inventory |
| Inventory Cell | `InventoryCell` | object reference (modifier 2) | - | -> GUI Inventory Cell |

### Interaction Object Properties

| property | key | type | default | |
|---|---|---|---|---|
| Distance to Add Interaction Objects to Test (lu) | `InteractionDist` | float | 5.0 |  |

### Push/Pull

| property | key | type | default | |
|---|---|---|---|---|
| Pushing Distance from Pushable (wu) | `PushDist` | float | 500.0 |  |
| Pulling Distance from Pushable (wu) | `PullDist` | float | 400.0 |  |
| Max Off-Axis Angle for Attach (deg) | `AttachAngle` | float | 30.0 |  |
| Idle Push | `PushIdle` | animation | - |  |
| Idle Pull | `PullIdle` | animation | - |  |
| Pulling | `PullAnim` | animation | - |  |
| Pushing | `PushAnim` | animation | - |  |
| Push To Pull Transition | `PushToPullTransitionAnim` | animation | - |  |
| Pull To Push Transition | `PullToPushTransitionAnim` | animation | - |  |

### Camera Options

| property | key | type | default | |
|---|---|---|---|---|
| Look-at Point Height Offset (wu) | `LookAtOffset` | float | 0.0 |  |
| Basic Tracking Distance (wu) | `TrackDist` | float | 2000.0 |  |
| Basic Tracking Height (wu) | `TrackHeight` | float | 500.0 |  |
| First Person X-axis Offset From Player (wu) | `FromPlayerOffsetX` | float | 0.0 |  |
| First Person Y-axis Offset From Player (wu) | `FromPlayerOffsetY` | float | 500.0 |  |
| First Person Z-axis Offset From Player (wu) | `FromPlayerOffsetZ` | float | 0.0 |  |
| Combat Camera Max Distance (lu) | `CombatCamDist` | float | 6.0 |  |
| Combat Camera Max Zoom Out (%) | `CombatCamZoom` | float | 50.0 |  |
| Distance From Camera To Starting Alphaing Player (lu) | `CamAlphaDist` | float | 0.75 |  |
| First Person Hit React Shake | `FirstPersonShake` | object reference (modifier 2) | - | -> Camera Shake |

### Targeting Parameters

| property | key | type | default | |
|---|---|---|---|---|
| Max Combat Target Distance (lu) | `MaxTargetLockDistance` | float | 6.0 |  |
| Sector Change to Initiate Dodge (0-4) | `AimDodgeDirectionChange` | int | 2 |  |
| Pause In Aimed State After Attack (sec.) | `AimStateTimeout` | float | 2.0 |  |
| Rotation Speed in Aim State (deg/sec) | `AimStateRotationSpeed` | float | 400.0 |  |
| Target's distance importance (0.0 - ?) | `TargetingDistWeight` | float | 1.0 |  |
| Target's screen center importance (0.0 - ?) | `TargetingScreenCenterWeight` | float | 1.0 |  |
| Target Button Toggle Time | `TargetingToggleTime` | float | 0.2 |  |
| Max Targeting Distance (LU) | `TargetingMaxDist` | float | 50.0 |  |
| Max Off-Screen Targeting Distance (LU) | `CriticalRadius` | float | 25.0 |  |
| Max Targeting FOV for Everyone (degs) | `TargetingPlayerToTargAng` | float | 180.0 |  |
| Auto Target Enemies Distance (LU) | `MeleeAutoTargetDist` | float | 2.0 |  |
| Projectile Auto-Target On-Screen Enemies Distance (lu) | `ProjAutoTargetDist` | float | 25.0 |  |
| Horizontal Targetable Screen Area (%) | `TargetingScreenXPerc` | float | 90.0 |  |
| Vertical Targetable Screen Area (%) | `TargetingScreenYPerc` | float | 90.0 |  |
| Channel for NPCs to target on player | `NPCTargettedChannel` | channel | none |  |

### Meters

| property | key | type | default | |
|---|---|---|---|---|
| Health Meter Options | `HealthMeter` | object reference (modifier 2) | - | -> HUD Variable Meter |
| Mana Meter Options | `ManaMeter` | object reference (modifier 2) | - | -> HUD Variable Meter |
| Ring Resistance Meter Options | `RingMeter` | object reference (modifier 2) | - | -> Ring Meter |
| Ring Icon | `RingIcon` | object reference (modifier 2) | - | -> Ring Icon |
| Current Spell Icon | `SpellIcon` | object reference (modifier 2) | - | -> 3d Icon |

### Combat Related

| property | key | type | default | |
|---|---|---|---|---|
|  | `SpecialAttackTimer` | float | 0.25 |  |
| Max Angle Allowed For Tap Directions (degrees) | `SpecialAttackTapMaxAngle` | float | 90.0 |  |
| Percent of Damage to Take While Blocking (%) | `BlockDamagePercent` | float | 25.0 |  |
| Must Be Facing Attacker Within Angle (degrees) | `BlockDamageMaxAngle` | float | 45.0 |  |

### Rolling Animations

| property | key | type | default | |
|---|---|---|---|---|
| Roll Left | `AimRollLeftAnim` | animation | - |  |
| Roll Right | `AimRollRightAnim` | animation | - |  |
| Roll Backward | `AimRollBackAnim` | animation | - |  |

### The One Ring

| property | key | type | default | |
|---|---|---|---|---|
| Maximum Purity | `MaxPurity` | float | 100.0 |  |
| Ring Death Movie | `RingDeathMovie` | object reference (modifier 2) | - | -> Movie Player |

### Speech

| property | key | type | default | |
|---|---|---|---|---|
| Jaw Channel | `JawChan` | channel | none |  |
| Max. Jaw Angle (deg) | `JawAngle` | float | 10.0 |  |
| JawChannel Axis | `JawChanAxis` | enum | 0 | `X-Axis` `Y-Axis` `Z-Axis` |
| Listening Animation | `ListenAnim` | animation | - |  |

### Conversation distances

| property | key | type | default | |
|---|---|---|---|---|
| Minimum distance to listener (lu) | `ConversationRadius` | float | 2.0 |  |
| Maximum height difference to initiate conversation (lu) | `VerticalThreshold` | float | 0.5 |  |
| Activation Angle (degrees) | `ConversationAngle` | float | 45.0 |  |
| Speed To Rotate In Conversations (deg/sec) | `ConversationRotationSpeed` | float | 100.0 |  |

### Facial Expressions

| property | key | type | default | |
|---|---|---|---|---|
| Eyebrow Channel | `EyebrowChan` | channel | none |  |
| Maximum Eyebrow Perturbation (0 - 3.14) | `MaxEyebrowAngle` | float | 0.1 |  |
| Eyelid Channel | `EyelidChan` | channel | none |  |
| Max Eyelid Perturbation (0 - 3.14) | `MaxEyelidAngle` | float | 0.06 |  |

### Head & Neck

| property | key | type | default | |
|---|---|---|---|---|
| Head Channel | `HeadChan` | channel | none |  |
| Neck Channel | `NeckChan` | channel | none |  |
| Upper Body Pitch Channel | `UBPitchChannel` | channel | none |  |
| Up Neck Limit | `NeckLimitUp` | float | 45.0 |  |
| Down Neck Limit | `NeckLimitDown` | float | 45.0 |  |
| Side Neck Limit | `NeckLimitSide` | float | 45.0 |  |
| Maxium Turn Angle | `MaxTurnAngle` | float | 7.69 |  |
| Upper Body Pitch Axis | `UBPitchAxis` | enum | 0 | `X-Axis` `Y-Axis` `Z-Axis` |
| Upper Body Negate Axis? | `UBPitchNegate` | enum | 1 | `No` `Yes` |
| Upper Body Pitch Limit (degs) | `UBPitchLimit` | float | 20.0 |  |
| Pitch Time (sec) | `PitchTime` | float | 0.2 |  |

### Magic

| property | key | type | default | |
|---|---|---|---|---|
| Initial Mana | `InitialMana` | float | 0.0 |  |
| Maximum Mana | `MaxMana` | float | 0.0 |  |
| No Mana | `NoManaSound` | sound | - |  |
| Default Spell | `DefaultSpell` | object reference (modifier 2) | - | -> Spell |

### Team Information

| property | key | type | default | |
|---|---|---|---|---|
| Team That The Player Is On | `Team` | object reference (modifier 2) | - | -> Team |

### PC Movement Animations

| property | key | type | default | |
|---|---|---|---|---|
| Strafing Left | `StrafeLeftAnim` | animation | - |  |
| Strafing Right | `StrafeRightAnim` | animation | - |  |
| Moving Back | `MoveBackAnim` | animation | - |  |
| Moving Forward-Left | `MoveForwardLeftAnim` | animation | - |  |
| Moving Forward-Right | `MoveForwardRightAnim` | animation | - |  |
| Moving Backward-Left | `MoveBackwardLeftAnim` | animation | - |  |
| Moving Backward-Right | `MoveBackwardRightAnim` | animation | - |  |
| Sneaking Forward-Left | `SneakForwardLeftAnim` | animation | - |  |
| Sneaking Forward-Right | `SneakForwardRightAnim` | animation | - |  |
| Dodge Forward | `DodgeForwardAnim` | animation | - |  |
| Dodge Back | `DodgeBackAnim` | animation | - |  |
| Dodge Left | `DodgeLeftAnim` | animation | - |  |
| Dodge Right | `DodgeRightAnim` | animation | - |  |

### PC Controls Configuration

| property | key | type | default | |
|---|---|---|---|---|
| Max Pitch (deg) | `MaxPitch` | float | 40.0 |  |
| Max FirstPerson Pitch (deg) | `FPMaxPitch` | float | 40.0 |  |
| Pitch Speed (deg/s) | `PitchSpeed` | float | 150.0 |  |
| Yaw (Turn) Speed (deg/s) | `YawSpeed` | float | 250.0 |  |
| One Ring Hotkey | `HKOneRing` | object reference (modifier 2) | - | -> The One Ring |
| Staff Strike Spell Hotkey | `HKStaffStrikeSpell` | object reference (modifier 2) | - | -> Spell |
| Fire Attack Spell Hotkey | `HKFireAttackSpell` | object reference (modifier 2) | - | -> Spell |
| Lightning Spell Hotkey | `HKLightningSpell` | object reference (modifier 2) | - | -> Spell |
| Heal Spell Hotkey | `HKHealSpell` | object reference (modifier 2) | - | -> Spell |
| Attract Spell Hotkey | `HKAttractSpell` | object reference (modifier 2) | - | -> Spell |

