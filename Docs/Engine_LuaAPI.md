# PainEngine Lua API (native functions)

Recovered from `Engine.dll` by scanning for `Script::RegisterFunction` call sites.
Addresses are virtual addresses at the DLL's preferred image base 0x10000000.

941 functions.

Functions are grouped by the registration table they belong to; one table
is one registration batch, which in practice means one engine module.

### Menu / GUI - 146 functions (table at 0x102B2570)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `ActivateLoadingScreen` | 0x1007F1C0 | yes |
| `ActivateMap` | 0x10074880 | yes |
| `Active` | 0x10074740 | yes |
| `AddBorder` | 0x10078C40 | yes |
| `AddCharPicker` | 0x100792A0 | yes |
| `AddCheckbox` | 0x10075C90 | yes |
| `AddColorPicker` | 0x10079050 | yes |
| `AddImageButton` | 0x100786D0 | yes |
| `AddImageButtonEx` | 0x100794F0 | yes |
| `AddImageToButtonEx` | 0x1007DE00 | yes |
| `AddImageToSlider` | 0x1007BAA0 | yes |
| `AddItemToList` | 0x1007BBD0 | yes |
| `AddKeyControl` | 0x100764C0 | yes |
| `AddKeyList` | 0x10078AE0 | yes |
| `AddLevelToMap` | 0x1007FB40 | yes |
| `AddList` | 0x10077D00 | yes |
| `AddLoadSave` | 0x10078110 | yes |
| `AddMapTable` | 0x10077FC0 | yes |
| `AddMapToServer` | 0x1007F9B0 | yes |
| `AddNumEdit` | 0x10077720 | yes |
| `AddNumRange` | 0x10078270 | yes |
| `AddPassword` | 0x10077A10 | yes |
| `AddPlayerModel` | 0x10078F00 | yes |
| `AddSaveGameToList` | 0x1007BD50 | yes |
| `AddScroller` | 0x10076C80 | yes |
| `AddServerList` | 0x100774A0 | yes |
| `AddServerToList` | 0x1007E3B0 | yes |
| `AddSimpleKeyConf` | 0x10076A20 | yes |
| `AddSlider` | 0x100761E0 | yes |
| `AddSliderImage` | 0x100771C0 | yes |
| `AddStaticText` | 0x10078500 | yes |
| `AddTabGroup` | 0x10078DA0 | yes |
| `AddTextButton` | 0x10075A40 | yes |
| `AddTextButtonEx` | 0x10075F00 | yes |
| `AddTextEdit` | 0x10076ED0 | yes |
| `AddWeaponList` | 0x10077E60 | yes |
| `ChangeTextButtonExValue` | 0x1007F830 | yes |
| `Clear` | 0x100747A0 | yes |
| `ClearList` | 0x1007F600 | yes |
| `ClearScreen` | 0x10074810 | yes |
| `DisableItem` | 0x1007C040 | yes |
| `EnableItem` | 0x1007C140 | yes |
| `EnableItemBG` | 0x1007D320 | yes |
| `FillMapTable` | 0x1007D490 |  |
| `GetAlternateKey` | 0x1007CE30 | yes |
| `GetCDKey` | 0x1007E210 | yes |
| `GetImageButtonExValue` | 0x1007CAE0 | yes |
| `GetListItems` | 0x1007C760 | yes |
| `GetListSeparatorPos` | 0x1007B3A0 | yes |
| `GetLoadingScreenOverall` | 0x100753A0 | yes |
| `GetMapsOnServer` | 0x1007DAB0 | yes |
| `GetNumRangeValue` | 0x1007C8E0 | yes |
| `GetPrimaryKey` | 0x1007CD10 | yes |
| `GetRegistryBonus1` | 0x100758F0 | yes |
| `GetRegistryBonus2` | 0x10075930 | yes |
| `GetRegistryBonus3` | 0x10075970 | yes |
| `GetSelectedMap` | 0x1007D920 | yes |
| `GetSelectedServerIP` | 0x1007E480 | yes |
| `GetSelectedServerPort` | 0x1007E5B0 | yes |
| `GetSelectedSGSlot` | 0x1007F520 | yes |
| `GetSimpleKey` | 0x1007CF50 | yes |
| `GetSliderValue` | 0x1007C9E0 | yes |
| `GetTextColor` | 0x1007F740 | yes |
| `GetTextEditValue` | 0x1007CBE0 | yes |
| `IsItemChecked` | 0x1007C340 | yes |
| `IsSliderFloat` | 0x1007C450 | yes |
| `JoinPKTV` | 0x1007FDD0 | yes |
| `JoinServer` | 0x1007EB10 | yes |
| `LaunchURL` | 0x1007F380 | yes |
| `LoadingProgress` | 0x10075420 | yes |
| `MapGetCurrChapter` | 0x100751D0 |  |
| `MapGetCurrLevel` | 0x10075150 |  |
| `MapGetCurrLevelCardCondition` | 0x1007F080 | yes |
| `MapGetCurrLevelCardIndex` | 0x100750C0 | yes |
| `MapGetCurrLevelName` | 0x1007F120 | yes |
| `MapGetLevelCardCondition` | 0x1007EFE0 |  |
| `MapNextLevel` | 0x10075050 | yes |
| `MapReset` | 0x10074FE0 | yes |
| `MapSetCurrLevel` | 0x10075250 | yes |
| `MoveListItemDown` | 0x1007C660 | yes |
| `MoveListItemUp` | 0x1007C560 | yes |
| `PauseSounds` | 0x100755D0 | yes |
| `PlayMovie` | 0x10074CF0 | yes |
| `PlaySound` | 0x1007F460 | yes |
| `RefreshServerList` | 0x1007D070 | yes |
| `RemoveAllMapsFromServer` | 0x1007D820 | yes |
| `RemoveMapFromTable` | 0x1007D6B0 |  |
| `ResetRoomType` | 0x100759B0 |  |
| `ResumeSounds` | 0x10075640 | yes |
| `ReturnToGame` | 0x10074DE0 | yes |
| `RunningInGame` | 0x1007DF80 |  |
| `SaveCDKey` | 0x1007E2E0 | yes |
| `SetAllowSave` | 0x1007F6A0 | yes |
| `SetBackground` | 0x1007D270 | yes |
| `SetBorderColCount` | 0x1007B620 | yes |
| `SetBorderColumn` | 0x1007B730 | yes |
| `SetBorderHeader` | 0x1007B850 | yes |
| `SetBorderScroller` | 0x1007B4E0 | yes |
| `SetBorderSize` | 0x1007AB30 | yes |
| `SetButtonGlowAnim` | 0x1007A9C0 |  |
| `SetCheckboxValue` | 0x1007C240 | yes |
| `SetItemAction` | 0x10079B50 | yes |
| `SetItemAlign` | 0x1007A470 | yes |
| `SetItemApplyRequired` | 0x1007A7A0 | yes |
| `SetItemColors` | 0x1007A200 | yes |
| `SetItemDesc` | 0x100798C0 | yes |
| `SetItemExitMovie` | 0x1007A350 | yes |
| `SetItemFonts` | 0x10079DE0 | yes |
| `SetItemFontsTex` | 0x1007A000 | yes |
| `SetItemPosition` | 0x10079A30 | yes |
| `SetItemsDrawShadow` | 0x10074BF0 | yes |
| `SetItemsFadeLength` | 0x10074B70 | yes |
| `SetItemSounds` | 0x1007AC50 | yes |
| `SetItemText` | 0x10079750 | yes |
| `SetItemVisibility` | 0x1007A680 | yes |
| `SetItemWarning` | 0x1007A8B0 | yes |
| `SetItemWidth` | 0x1007A570 | yes |
| `SetKeyItemIndex` | 0x1007AE50 | yes |
| `SetListBorderWidth` | 0x1007B180 | yes |
| `SetListMaxHeight` | 0x1007B070 | yes |
| `SetListSeparatorPos` | 0x1007B290 | yes |
| `SetLoadingScreenOverall` | 0x100752F0 | yes |
| `SetMenuWidth` | 0x100749E0 | yes |
| `SetMovieLoop` | 0x10074AE0 | yes |
| `SetProgressIcon` | 0x1007F2B0 | yes |
| `SetRegistryBonus1` | 0x100757A0 | yes |
| `SetRegistryBonus2` | 0x10075810 | yes |
| `SetRegistryBonus3` | 0x10075880 | yes |
| `SetScrollerForBorder` | 0x1007DC60 | yes |
| `SetScrollerHeight` | 0x10079CD0 | yes |
| `SetShowItemsFrame` | 0x10074C70 | yes |
| `SetSliderFullWidth` | 0x1007AF60 | yes |
| `SetStaticTextRect` | 0x1007B960 | yes |
| `SetTopPosition` | 0x10074A60 | yes |
| `SetWaitTime` | 0x100754B0 | yes |
| `ShowCredits` | 0x10075720 | yes |
| `ShowMenu` | 0x10074D70 | yes |
| `ShowMouse` | 0x10075540 | yes |
| `StartServer` | 0x1007E650 | yes |
| `StopServerList` | 0x1007D170 | yes |
| `StopSound` | 0x100756B0 | yes |
| `SwitchToBoard` | 0x10074F70 | yes |
| `SwitchToLevelSel` | 0x10074EC0 | yes |
| `SwitchToMap` | 0x10074930 | yes |
| `SwitchToMenu` | 0x10074E50 | yes |
| `UpdateMapTable` | 0x1007D590 | yes |

### World mesh / collision - 140 functions (table at 0x102C1B88)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `AddLight` | 0x101342D0 | yes |
| `AddRagdollToIntersectionSolver` | 0x10134830 | yes |
| `AddToIntersectionSolver` | 0x101349A0 | yes |
| `AttachTrailToBones` | 0x10142840 | yes |
| `CanLineTraceCollision` | 0x10134A60 |  |
| `ComputeChildMatrix` | 0x1012FCE0 | yes |
| `ComputeLocalPoint` | 0x1012FE40 | yes |
| `DamageItem` | 0x1013D8E0 | yes |
| `EnableCollisions` | 0x10130420 | yes |
| `EnableCollisionsToAll` | 0x1011DCE0 | yes |
| `EnableCollisionsToMesh` | 0x1011DDE0 | yes |
| `EnableCollisionsToRagdoll` | 0x10130500 | yes |
| `EnableDeathZoneTest` | 0x101361D0 | yes |
| `EnableDemonic` | 0x10136480 | yes |
| `EnableDraw` | 0x10136270 | yes |
| `EnableGunPass` | 0x10136550 | yes |
| `EnableLineTraceCollisions` | 0x101305E0 |  |
| `EnableNetworkSynchronization` | 0x1012F880 | yes |
| `EnableTick` | 0x10136360 | yes |
| `Exist` | 0x1012F740 | yes |
| `ExplodeItem` | 0x1013D670 | yes |
| `GetCenter` | 0x10132E60 | yes |
| `GetChildByName` | 0x10130370 | yes |
| `GetDimensions` | 0x10131F00 | yes |
| `GetFileName` | 0x10133080 | yes |
| `GetIndex` | 0x1012F5F0 | yes |
| `GetName` | 0x10132FE0 | yes |
| `GetOrientation` | 0x10132420 | yes |
| `GetPosition` | 0x10132BE0 | yes |
| `GetPtrByIndex` | 0x1012F690 | yes |
| `GetRotation` | 0x10132630 | yes |
| `GetRotationQ` | 0x101328A0 | yes |
| `GetSynchroString` | 0x1012FA20 | yes |
| `GetType` | 0x10132F40 | yes |
| `GetVelocity` | 0x10132020 | yes |
| `GetWorldPosition` | 0x10132D20 | yes |
| `IsDrawEnabled` | 0x10136620 | yes |
| `IsFixedMesh` | 0x10136110 | yes |
| `IsWater` | 0x10136050 | yes |
| `KillAllChildren` | 0x10130220 | yes |
| `KillAllChildrenByName` | 0x101302C0 | yes |
| `Pick` | 0x1011E070 | yes |
| `PO_AccumulateRotation` | 0x10133920 | yes |
| `PO_Activate` | 0x10130E40 | yes |
| `PO_AddAction` | 0x101309E0 | yes |
| `PO_Create` | 0x10130690 | yes |
| `PO_Enable` | 0x10130FA0 | yes |
| `PO_EnableGravity` | 0x10134B10 | yes |
| `PO_EnableSpeedDamping` | 0x10134CB0 | yes |
| `PO_Exist` | 0x10131320 | yes |
| `PO_GetAction` | 0x10130B40 |  |
| `PO_GetAngularDamping` | 0x101355B0 |  |
| `PO_GetCollisionGroup` | 0x101357B0 | yes |
| `PO_GetLinearDamping` | 0x10135500 |  |
| `PO_GetMass` | 0x10131800 | yes |
| `PO_GetMaxSphereRay` | 0x101307B0 | yes |
| `PO_GetPawnFloorPos` | 0x10133130 | yes |
| `PO_GetPawnHeadPos` | 0x10133270 | yes |
| `PO_GetPhysicsBody` | 0x10130860 | yes |
| `PO_GetType` | 0x10133350 | yes |
| `PO_HideFromPrediction` | 0x10136BE0 | yes |
| `PO_Hit` | 0x101337A0 | yes |
| `PO_Impulse` | 0x10133AA0 | yes |
| `PO_IsActionState` | 0x10130C90 | yes |
| `PO_IsActive` | 0x10130EF0 |  |
| `PO_IsEnabled` | 0x10131070 | yes |
| `PO_IsFixed` | 0x10134040 | yes |
| `PO_IsFlying` | 0x101340F0 | yes |
| `PO_IsOnFloor` | 0x101341C0 | yes |
| `PO_IsOnLadder` | 0x10136850 | yes |
| `PO_IsPinned` | 0x10133F90 | yes |
| `PO_JumpedInLastAction` | 0x10136900 | yes |
| `PO_LineTrace` | 0x101318B0 | yes |
| `PO_MaintainAngularVelocity` | 0x101350D0 |  |
| `PO_MaintainLinearMovement` | 0x10134DA0 | yes |
| `PO_MaintainPosition` | 0x10134EB0 | yes |
| `PO_MaintainVelocity` | 0x10134FC0 | yes |
| `PO_Move` | 0x10130D50 | yes |
| `PO_MoveCenterOfMass` | 0x10135910 |  |
| `PO_Remove` | 0x10135720 | yes |
| `PO_RemoveAction` | 0x10130A90 |  |
| `PO_Rotate` | 0x10131120 | yes |
| `PO_ScaleInertiaTensor` | 0x10131680 | yes |
| `PO_SetAction` | 0x10130BE0 | yes |
| `PO_SetAngularDamping` | 0x10135450 | yes |
| `PO_SetAsTransporter` | 0x101351E0 |  |
| `PO_SetCollisionGroup` | 0x10135860 | yes |
| `PO_SetEntitySteered` | 0x10133D80 | yes |
| `PO_SetFlying` | 0x10133C20 | yes |
| `PO_SetFreedomOfRotation` | 0x10135660 | yes |
| `PO_SetFriction` | 0x101352F0 | yes |
| `PO_SetGravity` | 0x10134BC0 | yes |
| `PO_SetGrenade` | 0x101369B0 | yes |
| `PO_SetHardDeactivator` | 0x10131760 | yes |
| `PO_SetLinearDamping` | 0x101353A0 | yes |
| `PO_SetMass` | 0x101315A0 | yes |
| `PO_SetMissile` | 0x10136A60 | yes |
| `PO_SetMonsterMovementConst` | 0x10130920 | yes |
| `PO_SetMonsterType` | 0x101313C0 | yes |
| `PO_SetMovedByExplosions` | 0x10133E30 | yes |
| `PO_SetPawnHeadPos` | 0x10133400 | yes |
| `PO_SetPinned` | 0x10133EE0 | yes |
| `PO_SetPlayerFlying` | 0x10133CD0 | yes |
| `PO_SetPlayerShocked` | 0x101367B0 | yes |
| `PO_SetRestitution` | 0x101314F0 | yes |
| `PO_SetSightParams` | 0x10131210 | yes |
| `PortalVisibilityTest` | 0x101366D0 |  |
| `RecreateRagdollIfNone` | 0x10134790 | yes |
| `RegisterChild` | 0x1012FAD0 | yes |
| `Release` | 0x10131460 | yes |
| `ReloadDecalSystem` | 0x1011DFE0 | yes |
| `RemoveFromIntersectionSolver` | 0x101348E0 | yes |
| `RemoveRagdoll` | 0x101346E0 | yes |
| `RemoveRagdollFromIntersectionSolver` | 0x10134630 | yes |
| `RenderBBox` | 0x10135FC0 | yes |
| `ResetLights` | 0x101343A0 | yes |
| `SeesEntity` | 0x101335E0 | yes |
| `SeesPoint` | 0x101334F0 | yes |
| `SetAmbient` | 0x10132AB0 | yes |
| `SetAngularVelocity` | 0x10132260 | yes |
| `SetKillByParent` | 0x1012FC40 | yes |
| `SetLocalBBox` | 0x10131DC0 | yes |
| `SetOrientation` | 0x10132350 | yes |
| `SetPosAndRotRelativeToCamera` | 0x1013D220 | yes |
| `SetPosition` | 0x10131C80 | yes |
| `SetRotation` | 0x10132520 |  |
| `SetRotationQ` | 0x10132780 | yes |
| `SetScale` | 0x10132A10 | yes |
| `SetSynchroString` | 0x1012F980 | yes |
| `SetTimeToDie` | 0x1012F7E0 | yes |
| `SetVelocity` | 0x10132140 | yes |
| `SpawnDecal` | 0x10135A00 | yes |
| `SpawnOrientedDecal` | 0x10135BE0 | yes |
| `SpawnStaticDecal` | 0x1013D420 |  |
| `Tick` | 0x10134580 | yes |
| `TransformLocalPointToWorld` | 0x1012FF80 | yes |
| `UnregisterAllChildren` | 0x10130180 | yes |
| `UnregisterChild` | 0x101300B0 | yes |
| `UpdateDecal` | 0x10135E60 |  |
| `UpdatePart` | 0x10136B20 | yes |

### World / level - 73 functions (table at 0x102C2010)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `AddEntity` | 0x10136C80 | yes |
| `AdvanceFrameCounter` | 0x1011E4F0 | yes |
| `AmbientColor` | 0x1011FB70 | yes |
| `ApplyTweaks` | 0x1011E410 | yes |
| `BloomFXParams` | 0x10120900 | yes |
| `CheckStartGlass` | 0x101201A0 | yes |
| `CreateEnabledAntiPortalFromClosedConvexMesh` | 0x10141DB0 | yes |
| `DeleteAntiPortal` | 0x1013E590 | yes |
| `DeleteDelayedEntities` | 0x10121070 | yes |
| `DeleteDyingEntities` | 0x101210E0 | yes |
| `DemonFXParams` | 0x10120820 | yes |
| `DemonFXWarp` | 0x101209E0 | yes |
| `EnableAntiPortal` | 0x1013E4D0 | yes |
| `EnableDeathZone` | 0x1013E180 | yes |
| `EnableDemonFX` | 0x10120720 | yes |
| `EnableDrawMeshGroup` | 0x10120B80 | yes |
| `EnableGhostsFX` | 0x101206A0 |  |
| `EnableOcclude` | 0x1011FEF0 | yes |
| `EnableOccludeAntiPortals` | 0x1011FF90 |  |
| `EnablePortal` | 0x1013E350 | yes |
| `EnableSuperDemonFX` | 0x101207A0 |  |
| `EnumerateAntiPortals` | 0x10137140 |  |
| `EnumeratePortals` | 0x10137080 |  |
| `Explosion2` | 0x1011ECF0 | yes |
| `ExplosionParabolic` | 0x1011F020 | yes |
| `ExplosionUp` | 0x1011EF20 | yes |
| `FindEntityByName` | 0x1013DD70 | yes |
| `FindEnvironmentAtPoint` | 0x101372D0 | yes |
| `FlashSkyTexture` | 0x101202F0 |  |
| `GetAmbientColor` | 0x1011FC80 | yes |
| `GetEntityList` | 0x10136DD0 |  |
| `GetFrameCounter` | 0x1011E480 |  |
| `GetLastExplodedEntities` | 0x10137200 | yes |
| `HitPhysicObject` | 0x1011EBD0 | yes |
| `Init` | 0x1011E140 | yes |
| `InitSky` | 0x1011FAF0 | yes |
| `IsAntiPortalEnabled` | 0x1013E410 |  |
| `IsGamePaused` | 0x1011F360 | yes |
| `IsPortalEnabled` | 0x1013E290 |  |
| `IsUnderwater` | 0x10120030 |  |
| `LateVBsBegin` | 0x1011E2E0 | yes |
| `LateVBsEnd` | 0x1011E320 | yes |
| `LineTraceFirstHit` | 0x1011EAC0 |  |
| `LineTraceFixedGeom` | 0x1011E8E0 | yes |
| `LineTraceHitPlayerBalls` | 0x1011E700 | yes |
| `LoadGame` | 0x10120EA0 | yes |
| `LoadLowQualitySky` | 0x1011F9B0 | yes |
| `LoadMap` | 0x1013DA10 | yes |
| `LoadSky` | 0x1011F860 | yes |
| `MakeUnderwater` | 0x101200B0 | yes |
| `MultiplayerExplosion` | 0x1011EE00 | yes |
| `RemoveEntity` | 0x10136D40 | yes |
| `SaveGame` | 0x10120DF0 | yes |
| `SetCollisionGroupMeshGroup` | 0x10120CD0 | yes |
| `SetDirLight` | 0x1011F160 | yes |
| `SetDrawDynLights` | 0x10120F50 | yes |
| `SetDynamicSpecular` | 0x10120380 | yes |
| `SetFarClipDist` | 0x1011FD50 | yes |
| `SetGamePaused` | 0x1011F250 |  |
| `SetGameVisible` | 0x1011F2C0 | yes |
| `SetInLoadingScreen` | 0x1011FDC0 | yes |
| `SetMaxFPS` | 0x10120A50 | yes |
| `SetPhysicsFPS` | 0x10120AE0 |  |
| `SetRenderTarget` | 0x10120580 | yes |
| `SetTimeToDeleteMeshGroup` | 0x10120C00 | yes |
| `SetupFog` | 0x10136EA0 | yes |
| `SetupSky` | 0x1011F7C0 |  |
| `SetupSkyLayer` | 0x1013DE90 | yes |
| `SetupWater` | 0x1011F3B0 | yes |
| `SetWorldSpeed` | 0x10120470 | yes |
| `SwitchToState` | 0x10121000 | yes |
| `UpdateAllEntities` | 0x10120130 | yes |
| `UseSwitchZones` | 0x1011FE70 | yes |

### Ragdoll / joints - 72 functions (table at 0x102C18A8)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `ApplyJointRotation` | 0x1012C130 | yes |
| `ApplyPointImpulseToRagdoll` | 0x1012B8C0 | yes |
| `ApplyPositionToJoint` | 0x1012D910 | yes |
| `ApplyRotationQuaternionToJoint` | 0x1012DCE0 |  |
| `ApplyRotationToJoint` | 0x1012DB30 | yes |
| `ApplyVelocitiesToAllJoints` | 0x1012D0D0 | yes |
| `ApplyVelocitiesToJoint` | 0x1012D400 | yes |
| `ApplyVelocitiesToJointLinked` | 0x1012D7B0 | yes |
| `BreakConstraintsForJoint` | 0x1012E2F0 |  |
| `CopyMatrixFromJointToJoint` | 0x1012C930 | yes |
| `CreateShadowMap` | 0x1012E9A0 | yes |
| `DrawJointNames` | 0x1012E900 |  |
| `EnableJoint` | 0x1012E130 | yes |
| `EnableNormalMaps` | 0x1012E500 | yes |
| `EnableRagdoll` | 0x1012B210 | yes |
| `EternallyFreeze` | 0x1012EB40 |  |
| `GetAnim` | 0x1013C1B0 | yes |
| `GetAnimLength` | 0x1012BD20 | yes |
| `GetAnimMovement` | 0x1012C210 | yes |
| `GetAnimTime` | 0x1012BDF0 | yes |
| `GetAnimTimeScale` | 0x1012C060 | yes |
| `GetClosestJoint` | 0x1012CD10 | yes |
| `GetJointFromHavokBody` | 0x1012D320 | yes |
| `GetJointIndex` | 0x1012C310 | yes |
| `GetJointName` | 0x1013C380 | yes |
| `GetJointPos` | 0x1012C530 | yes |
| `GetJointRotation` | 0x1012C7C0 | yes |
| `GetRagdollCollisionGroup` | 0x1012BAE0 | yes |
| `GetRagdollJointPos` | 0x1012C420 | yes |
| `GetRagdollJointRotation` | 0x1012C690 | yes |
| `GetVelocitiesFromJoint` | 0x1012D560 | yes |
| `IsPinned` | 0x1012DED0 | yes |
| `IsPinnedJoint` | 0x1012E060 | yes |
| `IsRagdoll` | 0x1012BA20 | yes |
| `IsRagdollActive` | 0x1012BC60 | yes |
| `JointsLinked` | 0x1012E200 | yes |
| `LineTrace` | 0x1012CEA0 | yes |
| `LoadAnim` | 0x1012B130 | yes |
| `MakeGib` | 0x10141C10 | yes |
| `MoveAllJoints` | 0x1012D220 | yes |
| `RagdollSelfExplosion` | 0x1012EBF0 | yes |
| `ResetFrame` | 0x1012B090 | yes |
| `ResetMaterialSpecular` | 0x1012E3B0 | yes |
| `SetAnim` | 0x1013BFC0 | yes |
| `SetAnimMovementCurve` | 0x1013C2A0 |  |
| `SetAnimTime` | 0x1012BF90 | yes |
| `SetAnimTimeScale` | 0x1012BED0 | yes |
| `SetBlendAlpha` | 0x1012E450 | yes |
| `SetHeadTrackCurve` | 0x1011DBE0 | yes |
| `SetHeadTrackRot` | 0x1011DBF0 | yes |
| `SetJointPositionLowLevel` | 0x1012DA20 | yes |
| `SetMaterial` | 0x1013CB10 | yes |
| `SetMaterialRefractFresnel` | 0x1013C5C0 | yes |
| `SetMaterialSpecular` | 0x1013C470 | yes |
| `SetMeshLighting` | 0x1013C870 | yes |
| `SetMeshVisibility` | 0x1013C780 | yes |
| `SetPinned` | 0x1012DE10 | yes |
| `SetPinnedJoint` | 0x1012DF90 | yes |
| `SetRagdollAngularDamping` | 0x1012B740 | yes |
| `SetRagdollBreakablesThreshold` | 0x1012B440 | yes |
| `SetRagdollCollisionGroup` | 0x1012BBA0 | yes |
| `SetRagdollDeactivationHardness` | 0x1012B800 |  |
| `SetRagdollFriction` | 0x1012B500 | yes |
| `SetRagdollHardDeactivator` | 0x1012B390 | yes |
| `SetRagdollLinearDamping` | 0x1012B680 | yes |
| `SetRagdollMovedByExplosions` | 0x1012B2D0 | yes |
| `SetRagdollRestitution` | 0x1012B5C0 |  |
| `SetTexture` | 0x1013C9F0 | yes |
| `SetTexturesPriority` | 0x1012EA60 |  |
| `SetWaterImpact` | 0x1012E5E0 | yes |
| `TransformPointByJoint` | 0x1012CA40 | yes |
| `UpdateWaterImpact` | 0x1012E770 | yes |

### Registered individually (direct RegisterFunction call sites) - 49 functions

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `_ALERT` | 0x1011C110 |  |
| `AddBitFlag` | 0x1012A580 | yes |
| `CreatePlayer` | 0x1012A420 | yes |
| `DoFile` | 0x1011C220 | yes |
| `DoString` | 0x1011C1B0 |  |
| `EulerToQuat` | 0x1011C390 | yes |
| `Exit` | 0x1011C1A0 | yes |
| `GetCDLabel` | 0x10121320 | yes |
| `GetCDLetter` | 0x1011C6E0 | yes |
| `GetDriveLetter` | 0x1012A8B0 | yes |
| `GetEngineVersionString` | 0x1012A4B0 | yes |
| `GetGCCount` | 0x1012AA30 | yes |
| `GetPlayerSpeed` | 0x1011DF50 | yes |
| `getregistry` | 0x1012A400 |  |
| `IsBitFlag` | 0x1012A600 | yes |
| `IsBlackEdition` | 0x101439C0 | yes |
| `IsBooHInstalled` | 0x10143950 | yes |
| `IsConstPhysicsTick` | 0x1012A7F0 |  |
| `IsDedicatedServer` | 0x1012A530 | yes |
| `IsFinalBuild` | 0x1012A7B0 | yes |
| `IsMPDemo` | 0x1012A830 | yes |
| `IsNewNetcode` | 0x1012A870 | yes |
| `IsPKInstalled` | 0x101438E0 | yes |
| `LabelOk` | 0x10121150 | yes |
| `Log` | 0x1011C2A0 | yes |
| `luaProfiler_LOGIC1` | 0x1012A250 | yes |
| `luaProfiler_LOGIC2a` | 0x1012A280 | yes |
| `luaProfiler_LOGIC2b` | 0x1012A2B0 | yes |
| `luaProfiler_LOGIC2c` | 0x1012A2E0 | yes |
| `luaProfiler_LOGIC3a` | 0x1012A310 | yes |
| `luaProfiler_LOGIC3b` | 0x1012A340 | yes |
| `luaProfiler_LOGIC3c` | 0x1012A370 | yes |
| `luaProfiler_LOGIC4a` | 0x1012A3A0 | yes |
| `luaProfiler_LOGIC5` | 0x1012A3D0 | yes |
| `MsgBox` | 0x1011C110 | yes |
| `NormalXToQuat` | 0x1012AAB0 | yes |
| `NormalYToQuat` | 0x1012ABC0 | yes |
| `NormalZToQuat` | 0x1012ACD0 | yes |
| `QuatToEuler` | 0x1011C470 | yes |
| `RemoveBitFlag` | 0x1012A690 | yes |
| `ReplaceBitFlag` | 0x1012A720 | yes |
| `RotateQuatByAxisAngle` | 0x1011C560 | yes |
| `SetPlayerSpeed` | 0x1011DEA0 | yes |
| `ToLightUD` | 0x1011C2B0 |  |
| `ToNumber` | 0x1011C320 |  |
| `VectorAngle` | 0x1012A120 |  |
| `VectorInverseRotateByQuat` | 0x1013B8D0 | yes |
| `VectorRotate` | 0x1013B660 | yes |
| `VectorRotateByQuat` | 0x1013B790 | yes |

### Render / debug draw - 40 functions (table at 0x102C2428)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `ApplyVideoSettings` | 0x1013F610 | yes |
| `ClearZBuffer` | 0x10123620 | yes |
| `ClosestPointOnLine` | 0x101226E0 | yes |
| `ComputeSZ` | 0x10122960 |  |
| `DistToCamera` | 0x10122300 | yes |
| `DistToLine` | 0x101225C0 | yes |
| `DistToLine2D` | 0x10122860 | yes |
| `DrawDirLight` | 0x10123450 | yes |
| `DrawPath` | 0x10122F50 |  |
| `DrawSphere` | 0x10123310 |  |
| `DrawSpotLight` | 0x10138070 |  |
| `DrawSprite` | 0x10122FE0 | yes |
| `DrawSprite1DOF` | 0x1013F170 | yes |
| `EnableBloom` | 0x101237C0 | yes |
| `EnableShadows` | 0x10123730 | yes |
| `EnableWarpEffects` | 0x101388A0 | yes |
| `Flip` | 0x10123A90 |  |
| `GetAvailableResolutions` | 0x1013F520 | yes |
| `GetCameraFOV` | 0x10123280 | yes |
| `GetFPS` | 0x10123BA0 | yes |
| `GetHWClass` | 0x10123940 | yes |
| `IsFullscreen` | 0x101238F0 | yes |
| `KeepDecals` | 0x10123B20 | yes |
| `RenderBox` | 0x10137EA0 | yes |
| `RenderLine` | 0x10122CC0 | yes |
| `RenderTranslucentBox` | 0x1013EE60 |  |
| `RGB` | 0x10122B70 | yes |
| `RGBA` | 0x10122C10 | yes |
| `ScreenSize` | 0x10122EA0 | yes |
| `ScreenTo3D` | 0x10137D80 | yes |
| `SetCameraFOV` | 0x101231C0 | yes |
| `SetContrastGammaAndBrightness` | 0x10123670 | yes |
| `SetParticlesDetail` | 0x10123A20 |  |
| `SetResolution` | 0x101429D0 |  |
| `SetTexFiltering` | 0x101239B0 | yes |
| `SetWaterQuality` | 0x10123860 | yes |
| `Spr_AddPoint` | 0x1013F450 | yes |
| `Spr_Create` | 0x1013F340 | yes |
| `Spr_Render` | 0x10142070 | yes |
| `VectorToScreen` | 0x10122A40 | yes |

### Network - 39 functions (table at 0x102C2278)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `BanClient` | 0x10121CC0 | yes |
| `CanConnectNewClient` | 0x10121990 | yes |
| `ClientConnected` | 0x10121D50 |  |
| `ClientPingReset` | 0x10121700 | yes |
| `Disconnect` | 0x10121BC0 | yes |
| `DisconnectClient` | 0x10121C30 | yes |
| `GetClientBandwidth` | 0x10122170 | yes |
| `GetClientID` | 0x10121B40 | yes |
| `GetClientPacketLoss` | 0x10121A70 | yes |
| `GetClientPing` | 0x10121630 | yes |
| `GetLastFrameLatency` | 0x10122200 | yes |
| `GetPushLatency` | 0x101218B0 |  |
| `GetSimulatedLatency` | 0x10121920 |  |
| `IsPlayingRecording` | 0x10121DB0 | yes |
| `IsSpectator` | 0x10121770 | yes |
| `LoadMapOnServer` | 0x1013EA70 | yes |
| `MsgCreate` | 0x1013E640 | yes |
| `MsgReadFromFile` | 0x1013E910 |  |
| `MsgReadVar` | 0x1013E6D0 | yes |
| `MsgSend` | 0x101214D0 | yes |
| `MsgWriteToFile` | 0x10141F20 |  |
| `MsgWriteVar` | 0x101373F0 | yes |
| `PredictionOn` | 0x10121E20 |  |
| `RecordPlayersInfo` | 0x10121DA0 |  |
| `SendEmptyPacket` | 0x10121A40 |  |
| `SendVariable` | 0x1013EC90 | yes |
| `SetClientBandwidth` | 0x101220D0 | yes |
| `SetEnemyPredictionInterpolation` | 0x10122280 | yes |
| `SetGameSpyVariable` | 0x1013EBC0 | yes |
| `SetPushLatency` | 0x10121910 |  |
| `SetServerFramerate` | 0x10122030 | yes |
| `SetSimulatedLatency` | 0x10121980 |  |
| `SetSpectator` | 0x10121810 | yes |
| `SetStatsNrToAvg` | 0x10121F00 | yes |
| `SetStatsShow` | 0x10121E70 | yes |
| `SetStatsUpdateDelay` | 0x10121F80 | yes |
| `SetupGameSpyVariable` | 0x1013EAF0 | yes |
| `UpdateFramerateLock` | 0x10121E60 |  |
| `UpdatePlayerData` | 0x10121600 |  |

### Sound system - 31 functions (table at 0x102C2698)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `ApplySoundSettings` | 0x101401B0 | yes |
| `Get3DSoundFalloff` | 0x10125170 |  |
| `Get3DSoundProviderName` | 0x1013FF40 | yes |
| `GetCurrent3DSoundProviderName` | 0x10140000 | yes |
| `GetNumOfProviders` | 0x101395A0 | yes |
| `GlobalSetLowPass` | 0x101253A0 | yes |
| `Play2D` | 0x10124510 | yes |
| `Play3D` | 0x10124610 | yes |
| `PreloadFile` | 0x10125430 | yes |
| `SaveGame_ResumeSounds` | 0x10125590 | yes |
| `Set3DSoundFalloff` | 0x101250E0 | yes |
| `Set3DSoundProvider` | 0x1013FE90 | yes |
| `Set3DSoundProviderByName` | 0x101400B0 |  |
| `SetMasterVolume` | 0x10125050 |  |
| `SetNextSuccesful3DSoundProvider` | 0x1013FDC0 |  |
| `SetObstructionProperties` | 0x10125600 |  |
| `SetPlayerOrientation` | 0x10124F70 | yes |
| `SetPlayerPos` | 0x10124E50 | yes |
| `SetRoomType` | 0x10125210 | yes |
| `SetSoundProperties` | 0x10140560 | yes |
| `SetSoundSpeedRandomizer` | 0x10125310 | yes |
| `StopAllSounds` | 0x10125520 |  |
| `StreamDelete` | 0x10124970 | yes |
| `StreamGetLowPass` | 0x10124C50 |  |
| `StreamGetVolume` | 0x10124AC0 | yes |
| `StreamLoad` | 0x101247A0 | yes |
| `StreamPause` | 0x10124D10 | yes |
| `StreamPlay` | 0x10124870 | yes |
| `StreamResume` | 0x10124DA0 | yes |
| `StreamSetLowPass` | 0x10124BA0 |  |
| `StreamSetVolume` | 0x10124A10 | yes |

### Input / timing - 28 functions (table at 0x102C1740)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `Action` | 0x1011C910 | yes |
| `BindKeyCommand` | 0x1013BE40 | yes |
| `GetActionStatus` | 0x1011CB70 | yes |
| `GetAsyncKeyState` | 0x1011D060 |  |
| `GetDIDeltaScale` | 0x1011D1E0 | yes |
| `GetEngNameByShortName` | 0x1013BD20 | yes |
| `GetKBType` | 0x1011D2E0 |  |
| `GetKeyNameByEngName` | 0x1013BB00 | yes |
| `GetLastTimeStep` | 0x1011CC60 |  |
| `GetShortNameByEngName` | 0x1013BC10 | yes |
| `GetTime` | 0x1011CBE0 | yes |
| `GetTimeDelta` | 0x1011CF60 | yes |
| `GetTimeFromTimerReset` | 0x1011CCE0 | yes |
| `GetTimeMultiplier` | 0x1011CE00 | yes |
| `GetUseDInput` | 0x1011D0E0 | yes |
| `IsFireSwitched` | 0x1011D010 | yes |
| `Key` | 0x1011C880 | yes |
| `LoadBindings` | 0x1011CED0 | yes |
| `Reinit` | 0x1011CFE0 | yes |
| `RemoveAction` | 0x1011CA50 | yes |
| `RemoveUIAction` | 0x1011CAE0 | yes |
| `Reset` | 0x1011CF40 | yes |
| `ResetTimer` | 0x1011CD60 | yes |
| `SetDIDeltaScale` | 0x1011D260 | yes |
| `SetTimeMultiplier` | 0x1011CD90 | yes |
| `SetUseDInput` | 0x1011D160 | yes |
| `TakeScreenshot` | 0x1011CE70 | yes |
| `UIAction` | 0x1011C9B0 | yes |

### Havok bodies / mesh groups - 24 functions (table at 0x102C2B70)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `ActiveMeshGroupActivate` | 0x10129D40 |  |
| `ActiveMeshGroupEnable` | 0x10129A80 | yes |
| `ActiveMeshGroupRecurrentActivationEnable` | 0x10129C90 |  |
| `ActiveMeshGroupSetActivationParams` | 0x10129DD0 | yes |
| `ActiveMeshGroupStaticMeshEnable` | 0x10129BE0 | yes |
| `GetHavokBodyActiveGroup` | 0x101298F0 | yes |
| `GetHavokBodyBarrierInfo` | 0x101292E0 | yes |
| `GetHavokBodyInfo` | 0x101291A0 | yes |
| `GetHavokBodyPosition` | 0x10129430 | yes |
| `GetHavokBodyRotation` | 0x10129000 |  |
| `GetHavokBodyVelocity` | 0x101290C0 | yes |
| `IsHavokBodyInWorld` | 0x101297D0 | yes |
| `IsHavokBodyPinned` | 0x10129740 | yes |
| `PinHavokBody` | 0x101296B0 | yes |
| `RemoveHavokBodyFromIS` | 0x10129860 | yes |
| `SetBunnyHopAcceleration` | 0x10129A00 | yes |
| `SetGravity` | 0x10129980 | yes |
| `SetHavokBodyPosition` | 0x10129360 | yes |
| `SetHavokBodyRotation` | 0x101294E0 |  |
| `SetHavokBodyVelocity` | 0x101295E0 | yes |
| `SetMaxRecursiveActivationDistance` | 0x10129F10 |  |
| `StaticMeshEnable` | 0x1013AAF0 | yes |
| `StaticMeshGroupEnable` | 0x10129B30 |  |
| `WarmUp` | 0x10129FB0 | yes |

### Lua stdlib: math - 23 functions (table at 0x102C6FC0)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `abs` | 0x10153630 | yes |
| `acos` | 0x10153720 |  |
| `asin` | 0x101536F0 |  |
| `atan` | 0x10153750 | yes |
| `atan2` | 0x10153780 | yes |
| `ceil` | 0x101537C0 | yes |
| `cos` | 0x10153690 | yes |
| `deg` | 0x10153990 |  |
| `exp` | 0x10153950 |  |
| `floor` | 0x101537F0 | yes |
| `frexp` | 0x101539F0 |  |
| `ldexp` | 0x10153A40 |  |
| `log10` | 0x10153920 |  |
| `max` | 0x10153B20 | yes |
| `min` | 0x10153AA0 | yes |
| `mod` | 0x10153820 | yes |
| `pow` | 0x101538A0 | yes |
| `rad` | 0x101539C0 |  |
| `random` | 0x10153BA0 | yes |
| `randomseed` | 0x10153D60 | yes |
| `sin` | 0x10153660 | yes |
| `sqrt` | 0x10153870 | yes |
| `tan` | 0x101536C0 | yes |

### Lua stdlib: base - 22 functions (table at 0x102C5F38)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `assert` | 0x1014A8C0 |  |
| `collectgarbage` | 0x1014A5B0 | yes |
| `error` | 0x1014A140 |  |
| `gcinfo` | 0x1014A560 |  |
| `getfenv` | 0x1014A3B0 | yes |
| `getmetatable` | 0x1014A1E0 | yes |
| `ipairs` | 0x1014A6B0 |  |
| `loadfile` | 0x1014A830 | yes |
| `loadstring` | 0x1014A7D0 | yes |
| `next` | 0x1014A630 | yes |
| `pairs` | 0x1014A670 |  |
| `pcall` | 0x1014A960 |  |
| `rawequal` | 0x1014A4C0 |  |
| `rawget` | 0x1014A4F0 | yes |
| `rawset` | 0x1014A520 | yes |
| `require` | 0x1014AD40 |  |
| `setfenv` | 0x1014A400 |  |
| `setmetatable` | 0x1014A230 | yes |
| `tostring` | 0x1014AA00 | yes |
| `type` | 0x1014A600 | yes |
| `unpack` | 0x1014A910 | yes |
| `xpcall` | 0x1014A9B0 |  |

### Filesystem (FS) - 18 functions (table at 0x102C2578)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `ClosePAK` | 0x10123FF0 | yes |
| `CloseZIP` | 0x10123F70 |  |
| `CreateDirectory` | 0x10123C10 | yes |
| `CreatePack` | 0x10123DF0 |  |
| `CreatePAK` | 0x10123F80 | yes |
| `CreateZIP` | 0x10123F00 |  |
| `DeleteFiles` | 0x10142F10 | yes |
| `ExtractPack` | 0x10123E80 |  |
| `File_Close` | 0x10124250 | yes |
| `File_Exist` | 0x10124080 | yes |
| `File_GetSize` | 0x10124100 | yes |
| `File_Open` | 0x10124000 | yes |
| `File_Write` | 0x10124180 | yes |
| `FindFiles` | 0x10142DD0 | yes |
| `GetBaseObjInfo` | 0x10123D00 | yes |
| `RegisterPack` | 0x101242C0 | yes |
| `RemoveDirectory` | 0x10123C90 | yes |
| `UnregisterPack` | 0x10124390 | yes |

### Transform / orientation - 18 functions (table at 0x102C29F8)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `EnableInterpolation` | 0x101279A0 | yes |
| `GetAng` | 0x10127A40 | yes |
| `GetAngRad` | 0x10127B20 | yes |
| `GetForwardVector` | 0x10127E20 | yes |
| `GetPos` | 0x10127D40 | yes |
| `GetQuaternion` | 0x1013A220 |  |
| `GetRawRotation` | 0x10127C90 | yes |
| `GetRightVector` | 0x10127EE0 | yes |
| `GetUpVector` | 0x10127FA0 | yes |
| `LookAt` | 0x10140EB0 | yes |
| `SetAdditionalRotation` | 0x10141170 | yes |
| `SetAng` | 0x10140CC0 | yes |
| `SetAngRad` | 0x10140DB0 | yes |
| `SetPos` | 0x10140BD0 | yes |
| `SetPositionDisplacement` | 0x10128060 | yes |
| `SetRotationDisplacement` | 0x10128140 | yes |
| `SetTransform` | 0x10140F90 | yes |
| `UpdateViewport` | 0x10128220 | yes |

### Materials / textures - 18 functions (table at 0x102C1AF0)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `AddSpecularLight` | 0x1012F490 | yes |
| `GetCubeMap` | 0x1012EF50 | yes |
| `GetDetailMap` | 0x1012EE80 | yes |
| `GetLightMap` | 0x1012F0B0 | yes |
| `GetNormalMap` | 0x1012F000 | yes |
| `GetRandomPoint` | 0x1012F250 | yes |
| `GetTextures` | 0x1012F160 | yes |
| `ResetSpecularLights` | 0x1012F550 | yes |
| `SetCubeMap` | 0x1013CEE0 | yes |
| `SetDefaultCubeMaps` | 0x1013CE40 | yes |
| `SetDefaultDetailMaps` | 0x1013CBF0 | yes |
| `SetDefaultMaterial` | 0x1012F340 | yes |
| `SetDefaultNormalMaps` | 0x1013D030 | yes |
| `SetDetailMap` | 0x1013CCC0 | yes |
| `SetLighting` | 0x1012EDE0 | yes |
| `SetMeshGroup` | 0x1012ED30 | yes |
| `SetNormalMap` | 0x1013D0D0 | yes |
| `SetSpecular` | 0x1012F3E0 | yes |

### Console / demo recording - 17 functions (table at 0x102AEF88)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `Activate` | 0x10029D70 | yes |
| `AddMessage` | 0x1002A7B0 | yes |
| `DemoGetLastFPS` | 0x10027CC0 |  |
| `DemoIsPlaying` | 0x10027BD0 | yes |
| `DemoPlay` | 0x10029E00 | yes |
| `DemoRecord` | 0x10027AC0 | yes |
| `DemoRecordPlayer` | 0x10027C20 | yes |
| `DemoStop` | 0x10027B70 | yes |
| `GetCurrentText` | 0x10028150 |  |
| `GetCursorPos` | 0x10027A20 |  |
| `IsActive` | 0x10027870 | yes |
| `Print` | 0x1002A930 | yes |
| `SetCurrentText` | 0x10029760 | yes |
| `SetFont` | 0x10028210 | yes |
| `SetMPMsgColor` | 0x100278C0 | yes |
| `SetMPMsgFont` | 0x10027FC0 | yes |
| `SetMPMsgPosition` | 0x10027980 | yes |

### Scoreboard - 15 functions (table at 0x102B0B78)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `ClearAll` | 0x1004F000 | yes |
| `Draw` | 0x1004EEC0 | yes |
| `GetPlayerTime` | 0x1004A960 |  |
| `GetTeamsScore` | 0x1004A600 | yes |
| `Hide` | 0x1004EE40 | yes |
| `RemovePlayer` | 0x1004ED40 | yes |
| `SetCaptureLimit` | 0x1004B6B0 | yes |
| `SetFragLimit` | 0x1004B5B0 | yes |
| `SetLMSLives` | 0x1004B7B0 | yes |
| `SetPlayerGameTime` | 0x1004A8C0 | yes |
| `SetTeamsScore` | 0x1004A6C0 | yes |
| `SetTimeLeft` | 0x1004B9B0 | yes |
| `SetTimeLimit` | 0x1004B8B0 | yes |
| `Show` | 0x1004EDC0 | yes |
| `Update` | 0x1004EBA0 | yes |

### Waypoints / AI paths - 15 functions (table at 0x102C2A98)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `Add` | 0x10142120 | yes |
| `DeleteSelected` | 0x101288B0 |  |
| `EnableDisableSet` | 0x10142290 | yes |
| `FastPickCurrentSet` | 0x1013A440 | yes |
| `FlipZ` | 0x10128D50 | yes |
| `GetClosest` | 0x10128DD0 | yes |
| `GetLength` | 0x101430D0 |  |
| `GetPathsNumber` | 0x1013A800 |  |
| `GetWaypointByPathNumber` | 0x10128F10 |  |
| `Load` | 0x10128A90 | yes |
| `MoveSelected` | 0x10128730 | yes |
| `RecalculateSelected` | 0x10128820 | yes |
| `Select` | 0x101284D0 | yes |
| `SelectFloors` | 0x10128940 | yes |
| `UnselectAll` | 0x10128640 | yes |

### HUD drawing - 14 functions (table at 0x102C2948)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `ColorSubstr` | 0x10140A90 | yes |
| `DrawBorder` | 0x10127510 | yes |
| `DrawBossHealth` | 0x10127480 | yes |
| `DrawQuad` | 0x10126F00 | yes |
| `DrawQuadRGBA` | 0x10127030 | yes |
| `DrawQuadRotated` | 0x101271B0 | yes |
| `DrawRect` | 0x10126E30 | yes |
| `GetTextHeight` | 0x101275D0 | yes |
| `GetTextWidth` | 0x101406A0 | yes |
| `GetTransparency` | 0x10127350 | yes |
| `PrepareString` | 0x10140840 | yes |
| `PrintXY` | 0x10126C60 | yes |
| `SetTransparency` | 0x101273D0 | yes |
| `StripColorInfo` | 0x10140970 | yes |

### Player / bot actions - 14 functions (table at 0x102C2620)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `AttachToUnderBody` | 0x10138F30 |  |
| `BotAction` | 0x10138C40 |  |
| `DetachFromUnderBody` | 0x10138FD0 | yes |
| `ExecAction` | 0x101389A0 | yes |
| `ExecMultiPlayerAction` | 0x10138B10 | yes |
| `FloorCheck` | 0x10138DA0 | yes |
| `FloorCheckMP` | 0x10138E60 | yes |
| `GetCameraFix` | 0x101394F0 | yes |
| `GetDistanceFromPoint` | 0x101393F0 | yes |
| `GetMPByte` | 0x10139120 | yes |
| `GetPitch` | 0x10139070 | yes |
| `RecordMovement` | 0x10139280 | yes |
| `SetMPByte` | 0x101391D0 | yes |
| `TestMovement` | 0x10139330 | yes |

### Sound instance - 14 functions (table at 0x102C27A0)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `DisableRandomize` | 0x101258D0 | yes |
| `Forget` | 0x101259D0 | yes |
| `GetLowPass` | 0x10125EE0 |  |
| `IsPlaying` | 0x10125AD0 | yes |
| `Pause` | 0x10125B60 | yes |
| `Play` | 0x10125850 | yes |
| `PlayAndForget` | 0x10125950 | yes |
| `Resume` | 0x10125BE0 |  |
| `SetHearingDistance` | 0x10126230 | yes |
| `SetLoopCount` | 0x10125CE0 | yes |
| `SetLowPass` | 0x10125E40 | yes |
| `SetObstructed` | 0x101262E0 | yes |
| `SetVolume` | 0x10125D80 | yes |
| `Stop` | 0x10125C60 | yes |

### Lua stdlib: string - 12 functions (table at 0x102C74D0)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `byte` | 0x10158170 | yes |
| `char` | 0x10158210 |  |
| `dump` | 0x101582F0 |  |
| `find` | 0x10158EC0 | yes |
| `format` | 0x10159810 | yes |
| `gfind` | 0x101591C0 | yes |
| `gsub` | 0x10159360 | yes |
| `len` | 0x10157E60 |  |
| `lower` | 0x10157F90 | yes |
| `rep` | 0x101580D0 | yes |
| `sub` | 0x10157EB0 | yes |
| `upper` | 0x10158030 | yes |

### Mouse - 10 functions (table at 0x102C1848)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `GetDelta` | 0x1011D460 | yes |
| `IsLocked` | 0x1011DB50 | yes |
| `LB` | 0x1011D5B0 | yes |
| `Lock` | 0x1011DAC0 | yes |
| `MB` | 0x1011D690 | yes |
| `RB` | 0x1011D620 | yes |
| `SetInverse` | 0x1011DA40 | yes |
| `SetSensitivity` | 0x1011D800 | yes |
| `SetSmooth` | 0x1011D920 | yes |
| `SetWheelSensitivity` | 0x1011D890 | yes |

### Lua stdlib: os - 10 functions (table at 0x102C6A78)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `clock` | 0x10151740 | yes |
| `date` | 0x10151870 | yes |
| `difftime` | 0x10151C40 |  |
| `execute` | 0x10151610 |  |
| `getenv` | 0x10151710 |  |
| `remove` | 0x10151650 | yes |
| `rename` | 0x10151680 |  |
| `setlocale` | 0x10151CD0 |  |
| `time` | 0x10151B20 |  |
| `tmpname` | 0x101516C0 |  |

### Lua stdlib: io - 10 functions (table at 0x102C6998)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `close` | 0x10150840 |  |
| `flush` | 0x10151470 | yes |
| `input` | 0x10150C60 |  |
| `lines` | 0x10151F50 | yes |
| `open` | 0x101509C0 |  |
| `output` | 0x10150C80 |  |
| `popen` | 0x10150A90 |  |
| `read` | 0x10151040 | yes |
| `tmpfile` | 0x10150AB0 |  |
| `write` | 0x10151270 | yes |

### Lua stdlib: debug - 9 functions (table at 0x102C6540)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `debug` | 0x1014D1C0 |  |
| `gethook` | 0x1014D100 |  |
| `getinfo` | 0x1014C970 | yes |
| `getlocal` | 0x1014CC40 |  |
| `getupvalue` | 0x1014CE70 |  |
| `sethook` | 0x1014D040 | yes |
| `setlocal` | 0x1014CD20 |  |
| `setupvalue` | 0x1014CE90 |  |
| `traceback` | 0x1014D270 |  |

### Lua stdlib: table - 9 functions (table at 0x102C77B0)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `concat` | 0x1015AA90 |  |
| `foreach` | 0x1015A7C0 |  |
| `foreachi` | 0x1015A710 | yes |
| `getn` | 0x1015A850 | yes |
| `insert` | 0x1015A910 | yes |
| `isconstant` | 0x1015A8F0 | yes |
| `setconstant` | 0x1015A8E0 |  |
| `setn` | 0x1015A890 | yes |
| `sort` | 0x1015B040 | yes |

### unidentified - 8 functions (table at 0x102C23D0)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `SetDynamicFlag` | 0x101378C0 | yes |
| `SetFakeSpecularFlag` | 0x10137970 | yes |
| `SetFalloff` | 0x10137720 | yes |
| `SetImportant` | 0x10137AD0 | yes |
| `SetIntensity` | 0x10137820 | yes |
| `SetLitParentFlag` | 0x10137A20 | yes |
| `SetProjector` | 0x1013ED90 | yes |
| `Setup` | 0x101375F0 | yes |

### unidentified - 8 functions (table at 0x102C2900)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `AddEmitter` | 0x10139BF0 | yes |
| `Die` | 0x10139A30 | yes |
| `Restart` | 0x1013A190 | yes |
| `SetEvolve` | 0x10139B10 | yes |
| `SetFixedTransform` | 0x1013A0F0 | yes |
| `SetImmortal` | 0x1013A050 | yes |
| `SetParentOffset` | 0x10139E30 | yes |
| `SetupEmitter` | 0x10139CB0 | yes |

### unidentified - 6 functions (table at 0x102C2C48)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `AddReflectMesh` | 0x10142770 | yes |
| `RemoveLight` | 0x1013B360 |  |
| `RemoveLights` | 0x1013B420 | yes |
| `ResetReflectList` | 0x10141670 | yes |
| `SetFog` | 0x1013ADB0 | yes |
| `SetWater` | 0x1013AEB0 | yes |

### unidentified - 5 functions (table at 0x102B015C)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `AddCard` | 0x10043040 | yes |
| `IsCardInSlot` | 0x10040900 | yes |
| `SetCashCheat` | 0x1003F6F0 | yes |
| `SetSlotPosition` | 0x1003F650 | yes |
| `SetupSlots` | 0x1003F550 | yes |

### unidentified - 4 functions (table at 0x102C1718)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `AddPoint` | 0x1013BA40 | yes |
| `Create` | 0x1012AF40 | yes |
| `Delete` | 0x10141B90 | yes |
| `GetBezierPoint` | 0x1012AFD0 | yes |

### unidentified - 4 functions (table at 0x102C2CB0)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `RemoveAllPlayers` | 0x1012A0E0 | yes |
| `SetGameMode` | 0x1012A050 | yes |
| `SetPlayerInfo` | 0x10141A50 | yes |
| `SetServerInfo` | 0x10141700 | yes |

### unidentified - 3 functions (table at 0x102C29C8)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `Replace` | 0x101277C0 | yes |
| `SetPriority` | 0x101278F0 |  |
| `Size` | 0x10127850 | yes |

### unidentified - 3 functions (table at 0x102C28C8)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `GetSound3DPtr` | 0x101397C0 | yes |
| `Setup3D` | 0x10139620 | yes |
| `SetVelocityScaleFactor` | 0x10139990 | yes |

### unidentified - 3 functions (table at 0x102C6008)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `status` | 0x1014B160 |  |
| `wrap` | 0x1014B110 |  |
| `yield` | 0x1014B140 |  |

### unidentified - 3 functions (table at 0x102C69F8)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `__gc` | 0x101508E0 |  |
| `__tostring` | 0x10150920 |  |
| `seek` | 0x10151340 |  |

### unidentified - 3 functions (table at 0x102C2B40)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `GetNextPoint` | 0x1013A8B0 | yes |
| `GetShortest` | 0x10142500 | yes |
| `IsFinished` | 0x1013AA20 | yes |

### unidentified - 1 functions (table at 0x102C2848)

| Lua name | native address | used by shipped scripts |
|---|---|---|
| `SetSoundSpeed` | 0x10126BC0 | yes |


