# Retail XML field references

This index preserves the source reading and confidence for the field ledger. Source identifiers refer to the original technical readings. They are provenance labels.

## Fields without a known retail reader

The classifier covers the owner and field together. H and M preserve high and medium confidence from the original reading.

| Owner and field | Source reading, section and confidence |
|---|---|
| `GameConstants.GUI_Tactical_Countdown_Timers_Screen_Spacing` | legacy docs/re/MCX4_PRES_RE_ANSWERS_2.md Q1.4 (read on a branch that cannot run) [H] |
| `GameObjectType.Include_In_UI_Map_Header`, `GameObjectType.Name_Adjust`, `GameObjectType.Planet_Ability_Name`, `GameObjectType.TSW_Post_Music_Wait_Frames`, `HardPoint.Tooltip_Text` | legacy docs/re/MCX4_PRES_RE_ANSWERS_2.md Q5 [M] |
| `Decal.Category` | legacy docs/re/MCX4_PRES_RE_ANSWERS_2.md Q6 Decal Category [H] |
| `LightSource.Blob_Color`, `LightSource.Blob_Intensity` | legacy docs/re/MCX4_PRES_RE_ANSWERS_2.md Q6 LightSource rows [H] |
| `GameObjectType.Apply_Y_Turret_Rotate_To_Axis`, `GameObjectType.Apply_Z_Turret_Rotate_To_Axis` | legacy docs/re/MCX4_PRES_RE_ANSWERS_2.md Q6 [H] |
| `GameObjectType.Pre_Lit` | legacy docs/re/MCX4_PRES_RE_ANSWERS_2.md Q6 [M] |
| `GameConstants.GUI_Planet_Flash_Level` | legacy docs/re/MCX4_PRES_RE_ANSWERS_3.md Q10 (only the ctor writes it) [H] |
| `GameConstants.MaximumSpecialStructuresLand`, `GameConstants.MaximumSpecialStructuresSpace` | legacy docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q2.1 [H] |
| `GameConstants.Income_Redistribution`, `GameConstants.Progressive_Taxation` | legacy docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q2.2 [H] |
| `GameConstants.Skirmish_Buy_Credits` | legacy docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q2.3 [H: read only in a pre-game branch single player never reaches] |
| `GameConstants.First_Strike_Extra_Damage_Percent`, `GameConstants.First_Strike_Particle` | legacy docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q2.4 [H] |
| `Faction.Credits_Accumulation_Factor` | legacy docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q7.1 [M] |
| `Faction.Corruption_Tactical_Build_Pad_Time_Multipliers` | legacy docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q7.2 [H] |
| `Faction.Can_Win_By_Destroying_Super_Weapon` | legacy docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q7.5 [H] |
| `GameObjectType.Disallows_Hyperspace_Retreat` | legacy docs/re/MCX4_RE_ANSWERS_TACTICAL.md §11 [H] |
| `Spawn_Ability.Self_Destruct_Radius` | legacy docs/re/MCX4_RE_ANSWERS_TACTICAL.md §14 [H: only the load check reads it] |
| `GameObjectType.Air_Vehicle_Turret_Target`, `GameObjectType.Ground_Infantry_Turret_Target`, `GameObjectType.Ground_Vehicle_Turret_Target`, `GameObjectType.Required_Timeline`, `GameObjectType.Size_Value`, `GameObjectType.Turret_Targets_Air_Vehicles`, `GameObjectType.Turret_Targets_Anything_Else`, `GameObjectType.Turret_Targets_Ground_Infantry`, `GameObjectType.Turret_Targets_Ground_Vehicles` | legacy docs/re/MCX4_RE_ANSWERS_TACTICAL.md §8 [H] |
| `GameConstants.AutomaticAutoResolve` | legacy docs/re/MCX4_T09_RE_ANSWERS.md Q3 AutomaticAutoResolve [H] |
| `GameObjectType.SpaceAutoResolveHitRate`, `GameObjectType.SpaceAutoResolveStunRate` | legacy docs/re/MCX4_T09_RE_ANSWERS.md Q4 [M: written, never read] |
| `GameObjectType.Is_Escort` | legacy docs/re/MCX4_T10_RE_ANSWERS.md §1 Round 3 [H: no caller of the getters] |
| `Attack_Action.Attack_Accuracy_Modifier` | legacy docs/re/MCX4_T10_RE_ANSWERS.md §11 [H] |
| `Arc_Sweep_Attack_Ability.Aggressiveness`, `Arc_Sweep_Attack_Ability.Focus` | legacy docs/re/MCX4_T10_RE_ANSWERS.md §14 Round 3 [M] |
| `Tractor_Beam_Attack_Ability.Damage_Application_Time_In_Secs` | legacy docs/re/MCX4_T10_RE_ANSWERS.md §15 [M: only the post-parse clamp reads it] |
| `GameConstants.MaxCombatAccuracyAlignmentBonus`, `GameConstants.MaxCombatDamageAlignmentBonus`, `GameConstants.MaxCombatSensorRangeAlignmentBonus` | legacy docs/re/MCX4_T10_RE_ANSWERS.md §16 [H] |
| `Faction.Land_Skirmish_Unit_Buy_Credits`, `Faction.Space_Skirmish_Unit_Buy_Credits` | legacy docs/re/MCX4_T10_RE_ANSWERS.md §17 [M] |
| `GameObjectType.Projectile_Acceleration_Per_Frame` | legacy docs/re/MCX4_T10_RE_ANSWERS.md §8 [H] |
| `GameObjectType.Formation_Priority` | legacy docs/re/MCX4_T11_T12_RE_ANSWERS.md §B2 Round 3 [H] |
| `GameObjectType.Space_Full_Stop_Command` | legacy docs/re/MCX4_T11_T12_RE_ANSWERS.md §B2 Round 3 [M] |
| `GameObjectType.Begin_Turn_Towards_Distance` | legacy docs/re/MCX4_T11_T12_RE_ANSWERS.md §B2 [H: read only by STARSHIP_LOCOMOTOR, which no base type lists] |
| `GameObjectType.CanCellStack` | legacy docs/re/MCX4_T11_T12_RE_ANSWERS.md §B2 [M] |
| `Eat_Attack_Ability.Needs_To_Rotate`, `Infection_Ability.Shoot_Anim` | legacy docs/re/MCX4_T11_T12_RE_ANSWERS.md §B3 [M] |
| `GameConstants.AI_LandThreatDistanceFactor`, `GameConstants.AI_LandThreatTurnRateFactor`, `GameConstants.AI_SpaceThreatDistanceFactor`, `GameConstants.AI_SpaceThreatTurnRateFactor`, `GameConstants.CrouchMoveBlendTime`, `GameConstants.Default_Defense_Adjust`, `GameConstants.MaxLandFormationFormupFrames`, `GameConstants.MaxObstacleCostLand`, `GameConstants.MaxObstacleCostSpace`, `GameConstants.ObstacleAreaOverlapForMaxSpace`, `GameConstants.SpaceIdlePathCullCoefficient`, `GameConstants.UseLinearCollisionChecks` | legacy docs/re/MCX4_T11_T12_RE_ANSWERS.md §S [M: export scan] |
| `Faction.Land_Surrender_SFXEvent`, `Faction.Space_Surrender_SFXEvent` | legacy docs/re/MCX4_T13_RE_ANSWERS.md Faction.Land_Surrender_SFXEvent family [H] |
| `Faction.Music_Event_Strategic_Lose`, `Faction.Music_Event_Strategic_Win` | legacy docs/re/MCX4_T13_RE_ANSWERS.md Faction.Music_Event_Strategic_Win family [H] |
| `Faction.Reinforcements_Requesting_SFXEvent` | legacy docs/re/MCX4_T13_RE_ANSWERS.md Faction.Reinforcements_Selection_SFXEvent family [H] |
| `Faction.SFXEvent_HUD_Advisor_Urgent` | legacy docs/re/MCX4_T13_RE_ANSWERS.md Faction.SFXEvent_HUD_Advisor_Urgent [M] |
| `Faction.SFXEvent_HUD_Build_Pad_Captured`, `Faction.SFXEvent_HUD_Build_Pad_Lost`, `Faction.SFXEvent_HUD_Landing_Zone_Captured`, `Faction.SFXEvent_HUD_Landing_Zone_Lost`, `Faction.SFXEvent_HUD_Last_Landing_Zone_Lost` | legacy docs/re/MCX4_T13_RE_ANSWERS.md Faction.SFXEvent_HUD_Landing_Zone_Captured family [H] |
| `Faction.SFXEvent_HUD_Enemy_Special_Weapon_Charging`, `Faction.SFXEvent_HUD_Special_Weapon_Charging` | legacy docs/re/MCX4_T13_RE_ANSWERS.md Faction.SFXEvent_HUD_Special_Weapon_Ready family [H] |
| `Faction.SpeechEvent_Super_Weapon_Enemy_Moving_Into_Range`, `Faction.SpeechEvent_Super_Weapon_Moving_Into_Range` | legacy docs/re/MCX4_T13_RE_ANSWERS.md Faction.SpeechEvent_Super_Weapon_Moving_Range_60_Seconds family [H] |
| `GameObjectType.SFXEvent_Bombard_Select_Target` | legacy docs/re/MCX4_T13_RE_ANSWERS.md GameObjectType.SFXEvent_Bombard_Incoming family [H] |
| `GameObjectType.SFXEvent_Defend`, `GameObjectType.SFXEvent_Stop` | #50 t02 decompile sweep: their only readers 0x44840 / 0x447a0 are never called or referenced [H] |
| `GameObjectType.SFXEvent_Deploy`, `GameObjectType.SFXEvent_Undeploy`, `GameObjectType.SFXEvent_Move_Opposite` | #50 t02 decompile sweep: only the type constructor 0x367250, destructor 0x36a070 and copy 0x36c000 touch +0x1000 / +0x1010 / +0x1020 [H] |
| `GameObjectType.SFXEvent_Unit_Under_Attack` | #50 t01 decompile sweep: only the type constructor 0x367250, destructor 0x36a070 and copy 0x36c000 touch +0x1220 [H] |
| `Faction.Carrier_Icon_Name`, `Faction.Corvette_Icon_Name`, `Faction.Fleet_Icon_Name`, `Faction.Frigate_Icon_Name`, `Faction.Ground_Base_Icon_Name`, `Faction.Ground_Transport_Icon_Name`, `Faction.Planet_Icon_Offset`, `Faction.Planet_Icon_Scale`, `Faction.Ship_Icon_Name`, `Faction.Squadron_Icon_Name`, `Faction.Star_Base_Icon_Name`, `Faction.Text_Nickname_ID`, `Faction.Vehicle_Icon_Name` | legacy docs/re/MCX4_T14_RE_ANSWERS.md Faction.Fleet_Icon_Name family [M] |
| `GameConstants.Battle_Pending_Message_Font`, `GameConstants.Battle_Pending_Message_Font_Size`, `GameConstants.Battle_Pending_Message_Pos_X`, `GameConstants.Battle_Pending_Message_Pos_Y` | legacy docs/re/MCX4_T14_RE_ANSWERS.md GameConstants.Battle_Pending_Message_Font family [H] |
| `GameConstants.CB_Flash_Duration` | legacy docs/re/MCX4_T14_RE_ANSWERS.md GameConstants.CB_Flash_Count [H] |
| `GameConstants.GUI_Hilite_Level` | legacy docs/re/MCX4_T14_RE_ANSWERS.md GameConstants.GUI_Cycle_Speed family [M: read, no effect] |
| `GameConstants.Galactic_Zoom_In_Station_Offset` | legacy docs/re/MCX4_T14_RE_ANSWERS.md GameConstants.Galactic_Zoom_In_Station_Rotation [H] |
| `GameConstants.In_Game_Message_Default_Font_Name`, `GameConstants.In_Game_Message_Default_Font_Size` | legacy docs/re/MCX4_T14_RE_ANSWERS.md GameConstants.In_Game_Message_Default_Font_Name family [H: read, no effect] |
| `GameConstants.Event_Message_Default_Font_Name`, `GameConstants.Event_Message_Default_Font_Size` | legacy docs/re/MCX4_T14_RE_ANSWERS.md GameConstants.In_Game_Message_Default_Font_Name family [H] |
| `GameConstants.Large_Coin_Stack_Size`, `GameConstants.Map_Preview_Image_Size`, `GameConstants.Medium_Coin_Stack_Size` | legacy docs/re/MCX4_T14_RE_ANSWERS.md GameConstants.Large_Coin_Stack_Size family [H] |
| `GameObjectType.GUI_Angles`, `GameObjectType.GUI_Distance`, `GameObjectType.GUI_Velocity`, `GameObjectType.GUI_X_Rot` | legacy docs/re/MCX4_T14_RE_ANSWERS.md GameObjectType.GUI_Angles family [M] |
| `GameObjectType.Tactical_Build_Start_Lower_Z` | legacy docs/re/MCX4_T14_RE_ANSWERS.md GameObjectType.Tactical_Build_Start_Lower_Z [M] |
| `Laser_Defense_Ability.Lightning_Effect_Name` | legacy docs/re/MCX4_T14_RE_ANSWERS.md Laser_Defense_Ability.Lightning_Effect_Name [M] |
| `Cable_Ability.Cable_Fadeout_Time` | legacy docs/re/MCX4_T15_RE_ANSWERS.md Cable_Ability.Cable_Width family [H] |
| `Campaign.Camera_Distance`, `Campaign.Camera_Shift_X`, `Campaign.Camera_Shift_Y` | legacy docs/re/MCX4_T15_RE_ANSWERS.md Campaign.Camera_Distance family [M] |
| `Eat_Attack_Ability.Rotation_Base_Bone_Name` | legacy docs/re/MCX4_T15_RE_ANSWERS.md Eat_Attack_Ability.Rotation_Base_Bone_Name [M] |
| `Faction.Land_Mode_Garrison_Selection_Blob_Material_Name`, `Faction.Space_Mode_Garrison_Selection_Blob_Material_Name` | legacy docs/re/MCX4_T15_RE_ANSWERS.md Faction.Land_Mode_Garrison_Selection_Blob_Material_Name family [M] |
| `Force_Confuse_Ability.Confuse_Effect` | legacy docs/re/MCX4_T15_RE_ANSWERS.md Force_Confuse_Ability.Confuse_Effect [M] |
| `GameConstants.Droid_Text_Color` | legacy docs/re/MCX4_T15_RE_ANSWERS.md GameConstants.Droid_Text_Color [H] |
| `GameConstants.LoopWaypointLineTextureName` | legacy docs/re/MCX4_T15_RE_ANSWERS.md GameConstants.WaypointLineTextureName family [H] |
| `GameObjectType.GUI_Model_Name` | legacy docs/re/MCX4_T15_RE_ANSWERS.md GameObjectType.GUI_Model_Name [M] |
| `GameConstants.RepushDistance`, `GameObjectType.MinimumPushReturnDistance` | MOVEMENT.md §5.4 [H] |
| `GameConstants.Galactic_Zoom_Acceleration`, `GameConstants.Max_Galactic_Zoom_Distance`, `GameConstants.Max_Galactic_Zoom_Speed`, `GameConstants.Min_Galactic_Zoom_Speed` | legacy docs/re/P1_CAMERA_INPUT.md §0 item 6 [M: no reader] |
| `Faction.SFXEvent_Build_Impossible_Location_Blockaded`, `Faction.SFXEvent_Max_Credits_Limit_Reached` | legacy docs/re/P4_SIM_SYSTEMS.md §2.7.3 [M: byte scan] |
| `GameConstants.Fleet_Maintenance_Update_Delay_Seconds` | legacy docs/re/STRATEGIC_GAPS.md §2.3 [H] |
| `GameConstants.Camera_Stop_Left`, `GameConstants.Camera_Stop_Right`, `GameConstants.Camera_Z_Position` | legacy docs/re/STRATEGIC_GAPS.md §7.1 [H] |
| `GameConstants.Health_Bar_Scale`, `GameConstants.Land_Health_Bar_Scale`, `GameConstants.Min_Health_Bar_Scale` | legacy docs/re/STRATEGIC_GAPS.md §7.2 [H] |
| `GameObjectType.Destroyed_Space_Tactical_Map` | legacy docs/re/TACTICAL_GAPS.md §4.10 [M: ctor / dtor / copy only] |
| `Faction.SFXEvent_HUD_Base_Shield_Penetrated`, `Faction.SFXEvent_HUD_Enemy_Base_Shield_Penetrated` | legacy docs/re/TRU1_A_RE_ANSWERS.md Penetration SFX [M: byte scan] |
| `Faction.Space_Retreat_Off_Map_Dest_Pos` | legacy docs/re/TRU1_C_RE.md §2.1 [H] |

## Other documented fields

Each entry lists tags named by the source reading. The compiled field ledger records their owner, parser code, and implementation state.

**legacy docs/re/A1_READINGS.md**

`AIUsesFogOfWarLand` `AI_Combat_Power` `Attack_Category_Restrictions` `Base_Shield_Radius` `Category` `Collidable_By_Projectile_Living` `Company_Transport_Unit` `Company_Units` `Create_Team` `Create_Team_Type` `Custom_Hard_XExtent` `Custom_Hard_XExtent_Deployed` `Custom_Hard_YExtent` `Custom_Hard_YExtent_Deployed` `Deploys` `Disable_Movement` `Disregard_Space_Layer_For_Guard_Attack_Move` `FormationSpacing` `Guard_Chase_Range` `Health` `Hover_Offset` `Initial_State_Visible_Under_FOW` `Is_Decoration` `Is_Valid_Target` `LandDestinationProximity` `LandPredictionTimeInterval` `Land_FOW_Reveal_Range` `Land_Guard_Range` `Landing_Transport_Variant` `Last_State_Visible_Under_FOW` `Max_Speed` `Max_Squad_Size` `MovementBoxExpansionFactor` `MovementPredictionInterval` `Projectile_Fire_Recharge_Seconds` `Ranged_Target_Z_Adjust` `Reachability` `Reinforcement_Deploy_Time_Multiplier` `Reinforcement_Enabling_Radius` `Reinforcement_Time_Multiplier` `Scale_Factor` `SetupPhaseEnabled` `Space_Guard_Range` `Space_Layer` `Space_Model_Name` `Squadron_Units` `Stay_In_Transport_During_Ground_Battle` `Stop_When_Activated` `Target_Bones` `Targeting_Fire_Inaccuracy` `Targeting_Fire_Inaccuracy_Fixed_Radius` `Targeting_Max_Attack_Distance` `Targeting_Priority_Set` `Targeting_Stickiness_Time_Threshold` `Type` `Unique_Ground_Unit`

**legacy docs/re/A2F_PRODUCTION_STALL_DIAG.md**

`Build_Time_Delay_Tolerance` `Time_Limit`

**legacy docs/re/A2F_RE_READS.md**

`Galactic_Build_Time_Multiplier` `Is_Neutral`

**legacy docs/re/A2F_T05_BUDGET_TRACE.md**

`Credit_Multiplier`

**legacy docs/re/A2GAL_READINGS.md**

`AI_Uses_Galactic_Pop_Cap` `Additional_Population_Capacity` `Base_Population_Capacity` `Fiscal_Cycle_Time_In_Secs` `Population_Value`

**legacy docs/re/A2H_GALACTIC_AI_RE.md**

`Build_Cost_Credits` `Duration` `Maintenance_Cost` `Required_Ground_Base_Level` `Variant_Of_Existing_Type`

**legacy docs/re/A2H_T02_SPACE_TOTAL.md**

`Garrison_Category`

**legacy docs/re/A2H_T06_TACTICAL_ETA.md**

`Build_Time_Seconds` `Land_Build_Time_Multiplier` `Production_Speed_Factor` `Space_Build_Time_Multiplier` `Tactical_Build_Time_Multiplier` `Tactical_Build_Time_Seconds`

**legacy docs/re/A2I_T01_FRAME_ORDER.md**

`AI_Player_Control` `Locations` `Max_Tech_Level` `Starting_Forces`

**legacy docs/re/A2I_TOKENS_RE.md**

`CategoryMask` `Credit_Gain_Factor` `Is_Special_Weapon_In_Space` `MaximumPoliticalControl` `Political_Income_Curve` `Space_Or_Garrison_Category` `Starting_Spawned_Units_Tech_0`

**legacy docs/re/A2J_T02_PROBE_PLAN.md**

`Lua_Script`

**legacy docs/re/A2J_T03_INCOME.md**

`Corruption_Choice_Income_Percentage`

**legacy docs/re/A2J_T07_LITERALS.md**

`AIUsesFogOfWarGalactic`

**legacy docs/re/A2K_T01_BUILD_COST_RE.md**

`Activation_Style` `Applicable_Unit_Categories` `Base_Level` `Benefits_From_Corruption` `Build_Advances_Tech_Level` `Build_Time_Reduced_By_Multiple_Factories` `Corruption_Galactic_Production_Price_Multipliers` `Corruption_Galactic_Production_Time_Multipliers` `Excluded_Unit_Types` `Galaxy_Wide` `Max_Build_Queue` `Price_Reduction_Percentage` `Production_Speed_Mod_Base_Vs_Tech_0` `Required_Special_Structures` `Required_Star_Base_Level` `Stacking_Category` `Tech_Level`

**legacy docs/re/A2K_T02_RE_ANSWERS.md**

`Galactic_AI_Contrast_Multiplier` `MaximumFleetMovementDistance`

**legacy docs/re/A2K_T06_RE.md**

`Function`

**legacy docs/re/A2K_T07_STORY_LOAD_RE.md**

`Create_Additional_Object` `Reward_Param3`

**legacy docs/re/A2K_T10_CREDIT_NOTIFY_RE.md**

`Cheap_Bribe_Cost` `Expensive_Bribe_Cost` `Planet_Capture_Bonus_Reward`

**legacy docs/re/A2L_T03_RE_ANSWERS.md**

`Health_Low_Percent_Threshold`

**legacy docs/re/A2M_R2_RE_A.md**

`Activation_Tracking_Duration` `Galactic_AI_Goal_Cycle_Sleep_Duration` `Max_Space_Base` `Per_Activation_Failure_Desire_Adjust` `Per_Failure_Desire_Adjust` `Special_Structures_Land` `Tracking_Duration`

**legacy docs/re/A2M_R2_RE_B.md**

`Apply_To_All_Allies` `Bounty_Percentage_Amount` `Default_Bounty_By_Category_SP` `Execute_Script_On_Type` `Override_Bounty_MP` `Override_Bounty_SP` `Target_Faction_Names`

**legacy docs/re/A2M_R2_UW_BOUNTY_TRACE.md**

`Automatic_Planetary_Corruption_Upgrade_Type`

**legacy docs/re/A2M_T04_RE_ANSWERS.md**

`Behavior` `GalacticBehavior` `High_Threat_Reachability_Tolerance` `Is_Destroyable` `Is_Generic_Hero` `LandBehavior` `Low_Threat_Reachability_Tolerance` `Max_Ground_Forces_On_Planet` `Medium_Threat_Reachability_Tolerance` `Overrun_Auto_Resolve_Multiple` `Should_Be_Destroyed_When_All_Hardpoints_Destroyed`

**legacy docs/re/A2M_T07_REBEL_SLICE_RUN.md**

`Planet_Surface_Accessible`

**legacy docs/re/A2M_T08_T10_T14_RE.md**

`Home_Location` `Is_Named_Hero` `Planet_Auto_Reveal` `Planet_Tech_Availability_Rating` `Slice_Cost_Credits`

**legacy docs/re/ABILITIES_HARDPOINTS.md**

`Abilities` `Activation_Max_Range` `Active_By_Default` `Allow_Opportunity_Fire_When_Idle` `Applicable_Unit_Types` `Area_Effect_Decal_Distance` `Armor_Type` `Attachment_Bone` `Attachment_Frame` `Attack_Anim_Sub_Index` `Attack_Animation` `Attack_Animation_Is_Overlay` `Attack_Animation_Speedup` `Attack_Animation_Subindex` `Begin_Applying_Damage_Frame` `Berserker_Duration_In_Secs` `Block_Chance` `Bomb_Countdown_Seconds` `Bone_Names` `Converted_To_Enemy_Die_Time_Seconds` `Damage_Frame_Number` `Damage_Particles` `Damage_Percent_When_Activated` `Damaged_Smoke_Asset_Name` `Death_Breakoff_Prop` `Death_Clone` `Death_Explosion_Particles` `Death_Fade_Time` `Death_Persistence_Duration` `Death_Projectiles` `Defense_Duration_In_Secs` `Destroy_When_Stunned_Over_Water` `Destruction_Survivors` `Duration_In_Secs` `Effective_Radius` `EnergyRechargeIntervalInSecs` `EnergyToShieldExchangeRate` `Energy_Capacity` `Energy_Refresh_Rate` `Engine_Particles` `Expiration_Seconds` `Face_Target` `Faction_Anim_Subindex` `Fire_Delay` `Fire_Frame` `Fire_Max_Recharge_Seconds` `Fire_Pulse_Count` `Fire_When_Undeployed` `Firing_Cone` `Force_Ability_Disable_Time` `Force_Sight_Duration_In_Secs` `Friendly_Ability` `GUI_Activated_Ability_Name` `HardPoints` `Hardpoint_Recharge_Cutoff_For_Opportunity_Fire` `Immune_To_Damage` `Initially_Enabled` `Ion_Storm_Shield_Disable_Time` `Is_Combustible` `Is_Pulsing` `Loop_Idle_Anim_00` `Max_Num_Spawned_Objects` `Max_Number_Of_Pulses` `Max_Projectile_Redirection_Angle_In_Degrees` `Max_Rate_Of_Turn` `Max_Remote_Bombs_Per_Player` `Mod_Flag` `Mod_Multiplier` `Must_Be_Bought_On_Black_Market` `Nebula_Ability_Disable_Time` `Object_Type` `Owner_Attachment_Bone` `Particle_Effect` `Passive_Missile_Shield_Radius` `Projectile_Category` `Projectile_Disables_Engines_When_Power_Drained` `Projectile_Dist_Travelled` `Projectile_Does_Energy_Damage` `Projectile_Energy_Per_Shot` `Projectile_Ion_Stun_On_Detonation` `Projectile_Stun_Radius` `Projectile_Types` `Projectile_Types_Override` `Projectile_Types_Targeted` `Protection_Radius` `Pulse_Frequency_Secs` `Reaction_Arc_In_Degrees` `Recharge_Seconds` `Recharge_Time_In_Secs` `Redirect_Chance` `Requires_Base_Power` `SFXEvent_Unit_Lost` `Sensor_Jamming_Time` `Shield_Off_Anim` `Shield_On_Anim` `Space_FOW_Reveal_Range` `Spawn_Frame` `Spawned_Object_Type` `Spin_Away_On_Death_Time` `Stealth_Detection_Time` `Stealth_Duration_Multiplier` `Stealth_Transition_Time` `Supports_Autofire` `Target_Damage_Increase_Percent` `Target_Destruction_Frame` `Target_Grabbed_Animation` `Target_Position_Z_Offset` `Target_Speed_Decrease_Percent` `Target_Stealth_Units` `Target_Types` `Targeting_Allowed_When_Burning` `Targeting_Fire_Inaccuracy_Fixed_Radius_Override` `Toss_Anim` `Unit_Abilities_Data`

**legacy docs/re/ABILITY_PARTICLES.md**

`Base_Shield_Penetration_Particle` `Blob_Color` `Charging_Time_In_Seconds` `Drain_Effect_Name` `Drain_Radius` `Drain_Source_Bone_Name` `Drain_Target_Bone_Name` `Duration_In_Seconds` `Heal_Interval_In_Secs` `Heal_Range` `Heal_Range_Blob_Material` `Infection_Particle` `Lightning_Effect_Name` `Object_Visual_Status_Particle_Attach_Bone_Names` `Owner_Particle_Bone_Name` `Particle_Lifetime_Frames` `Projectile_Stun_Spawn_Effect` `SFXEvent_Activate` `SFXEvent_Target_Affected` `Single_Target_Heal` `Stunned_Effect` `Target_Particle_Bone_Name` `Target_Particle_Effect`

**legacy docs/re/ABILITY_VISUALS.md**

`Beam_Bone_Name` `Beam_Color` `Beam_Frames` `Beam_Texture` `Beam_Texture_Name` `Beam_Width` `Blob_Material_Name` `Damage_Per_Frame` `Energy_Beam_Color` `Energy_Beam_Frames` `Energy_Beam_Texture` `Energy_Beam_Width` `Fire_Continuation` `Force_Cloak_Transition_In_Secs` `Laser_Beam_Frames` `Laser_Beam_Texture` `Laser_Beam_Width` `Laser_Color` `Line_Color` `Line_Width` `Num_Anim_Frames` `Owner_Particle_Effect` `Projectile_Custom_Render` `Show_Blob_To_Enemies` `Stealth_Color` `Tractor_Beam_Color` `Tractor_Beam_Frames` `Tractor_Beam_Texture` `Tractor_Beam_Width` `Turn_Rate`

**legacy docs/re/AI_GALACTIC.md**

`GameMode` `Global_Exclusions` `Name` `Priority`

**legacy docs/re/AI_GALACTIC_2.md**

`Markup_Filename`

**legacy docs/re/AI_GALACTIC_3.md**

`Allow_Reinforcement_Percentage_Normalized` `Cinematic_Object_Only` `Land_Override_Population_Value` `Land_Skirmish_Unit_Cap_By_Player_Count` `MovementClass` `Planet_Restricted_Unit_Categories` `Planet_Restricted_Unit_Types` `Space_Override_Population_Value` `Space_Tactical_Unit_Cap` `Tactical_Build_Cost_Multiplayer` `Tactical_Build_Prerequisites` `Tactical_Production_Queue` `Terrain`

**legacy docs/re/AI_GALACTIC_TOKENS.md**

`MaximumGroundbaseLevel` `MaximumSpecialStructures` `MaximumStarbaseLevel` `Potential_Indigenous_Power` `Tactical_Build_Attachment_Bone_Name` `Tactical_Buildable_Constructed` `Tactically_Built_Child_Object_Persists`

**legacy docs/re/AI_TACTICAL.md**

`AI_FogCellsPerThreatCell` `AI_LandEvaluatorRegionSize` `AI_SpaceAreaThreatScaleFactor` `AI_SpaceEvaluatorRegionSize` `Attack_Move_Response_Range` `Auto_Deploys` `Deployment_Anim_Rate` `Destroy_When_Child_Dies` `Has_Land_Evaluator` `Has_Space_Evaluator` `Idle_Chase_Range` `Influences_Capture_Point` `Is_Community_Property` `Is_Dummy` `Land_AI_Contrast_Multiplier` `Minimum_Time_Before_Pad_Can_Build_Again` `Radius` `Reinforcement_Prevention_Radius` `Space_Reinforcement_Collision_Check_Distance` `Use_Reinforcement_Points` `Victory_Relevant` `Weather_Category`

**legacy docs/re/AI_TACTICAL_PERCEPTION.md**

`AI_SpaceThreatDecayStep` `DesiredLandFOWCellSize` `DesiredSpaceFOWCellSize` `Faction`

**legacy docs/re/ANIM_PARTICLES.md**

`Allow_Particle_Model_Culling` `Cinematic_Anim_Blend_Seconds` `Cinematic_Anim_Speed` `Deployed_Walk_Anim_Speed` `Idle_Anim_00_Rate_Mod` `InfantryTurnBlendTime` `Land_Model_Anim_Override_Name` `Max_Flinch_Blend_Seconds` `Max_Flinch_Hold_Seconds` `MoveBlendTime` `Movement_Animation_Speed` `No_Particle_Below_Detail_Level` `Remove_Upon_Death` `Space_Model_Anim_Override_Name` `Specific_Death_Anim_Index` `Specific_Death_Anim_Type` `WalkAnimationCutoff` `Walk_Transition`

**legacy docs/re/AUDIO.md**

`Bink_Player_Caption_Font_Name` `Build_Music_Completed` `Caption_Duration` `Chained_SFXEvent` `Click_SFX` `Death_SFXEvent_Start_Die` `Delay_Between_Land_Base_Attack_Announcement_Seconds` `Emitter_Intensity` `Faction_Super_Weapon` `Fade_In_Seconds` `Fade_Out_Previous_Seconds` `Files` `Fire_SFXEvent` `Is_2D` `Is_3D` `Is_Ambient_VO` `Is_GUI` `Is_HUD_VO` `Is_Playable` `Is_Preset` `Is_Unit_Response_VO` `Kills_Previous_Object_SFX` `Localize` `Loop` `Loop_Fade_In_Seconds` `Loop_Fade_Out_Seconds` `MSS_3D_Provider_Name` `MSS_Internal_Loop` `Max_Instances` `Max_Pan2D` `Max_Pitch` `Max_Postdelay` `Max_Predelay` `Max_Volume` `Min_Pan2D` `Min_Pitch` `Min_Postdelay` `Min_Predelay` `Min_Volume` `Mouse_Over_SFX` `Music_Event_Battle_Load_Screen` `Music_Event_List_Ambient` `Music_Event_List_Battle` `Music_Land_Battle_To_Ambient_Peace_Seconds` `Overlap_Test` `Play_Count` `Play_Sequentially` `Post_Samples` `Pre_Samples` `Probability` `Projectile_Appearance_Delay_Frames` `Projectile_SFXEvent_Detonate` `Render_Mode` `Right_Click_SFX` `SFXEvent_Arrive_From_Hyperspace` `SFXEvent_Build_Started` `SFXEvent_Exit_Into_Hyperspace` `SFXEvent_Fire` `SFXEvent_GUI_Negative_Feedback` `SFXEvent_GUI_Unit_Ability_Activated` `SFXEvent_Land_Base_Under_Attack_Announcement` `SFXEvent_Land_Invasion_Commencing` `SFXEvent_Retreat_Engines_Jump` `SFXEvent_Select` `SFXEvent_Shields_Go_Back_Up` `SFXEvent_Shields_Go_Down` `SFXEvent_Space_Base_Under_Attack_Announcement` `SFXEvent_Special_Ability_Loop` `SFXEvent_Tactical_Corruption_Mission_Commencing` `SFXEvent_Target_Ability` `SFXEvent_Weather_Begin` `SFXEvent_Weather_End` `Samples` `SpeechEvent_Tactical_Intro_Land_Attacker_Last_Location` `Strategic_Map_Music_Event` `TSW_MusicEvent_Activation_Start` `Tactical_Intro_Command_Bar_Movie_Name` `Test_Disable_Weapon_Fire_SFX` `Text_ID` `Use_Preset` `Volume_Percent` `Volume_Saturation_Distance`

**legacy docs/re/B0173_DEPLOYED_BEHAVIOR_RE.md**

`DeployedBehavior` `Max_Dist_Walk_While_Deployed`

**legacy docs/re/BATTLE_TRANSITIONS.md**

`Additional_Garrison_Units` `AlwaysBypassAutoResolve` `AutoResolveAttritionAllowanceFactor` `AutoResolveDisplayTime` `AutoResolveLoserAttrition` `AutoResolveTransportLosses` `AutoResolveWinnerAttrition` `Auto_Resolve_Tactical_Multiplier` `Autoresolve_Exclusion_Locations` `Can_Participate_In_Space_Battle` `Garrison_Reinforcement_Delay_Seconds` `HS_Speed_Factor` `In_Game_Cinematics` `Is_Autoresolve_Allowed` `Is_Multiplayer` `Is_Super_Weapon_Killer` `Land_Auto_Resolve_Delay_Seconds` `Land_Base_Destruction_Forces_Retreat` `Land_Bomber_Type` `Land_Tactical_Map` `Land_Victory_Relevant` `MP_Default_Allow_Auto_Resolve` `Prevents_Story_Campaign_Autoresolve` `RetreatAutoResolveLoserAttrition` `RetreatAutoResolveWinnerAttrition` `Space_Retreat_Begin_SFXEvent` `Space_Retreat_Countdown_Seconds` `Space_Retreat_Flight_Move_Increment` `Space_Retreat_Unit_Increment_Wait_Frames` `Space_Station_Destruction_Forces_Retreat` `Space_Tactical_Map` `Space_Victory_Relevant` `Strategic_Queue_Tactical_Battles` `Unique_Space_Unit`

**legacy docs/re/BKG1_RE_ANSWERS.md**

`AIGoalApplicationFlags` `Build_Initially_Locked` `Capture_Point_Radius` `Capture_Point_Transition_Time_Seconds` `Hyperspace` `Icon_Name` `Is_Marker` `Is_Visible_On_Radar` `Land_Model_Name` `Ownership_Sticks` `Pay_As_You_Go` `Planet_Max_Political_Control_For_Faction` `Planet_Retains_Residual_Influence` `Political_Control` `Political_Control_Gain` `Political_Faction` `Required_Orbiting_Units` `Required_Planets` `Skirmish_Reinforcement_Delay_Frames` `Tactical_Build_Cost_Campaign`

**legacy docs/re/BKG3_RE_ANSWERS.md**

`Corruption_Hyperspace_Bonus` `Is_Like` `Marker_For_Specific_Object_Type` `Next_Level_Base` `Point_A` `Point_B` `Space_Skirmish_AI_Default_Forces` `TradeRouteMovementFactor` `Trade_Routes`

**legacy docs/re/BKR1_RE_ANSWERS.md**

`ApproximationForwardDistance` `FramesPerPositionApproximationRebuild` `MinLandPredictionDistance` `Targeting_Min_Attack_Distance`

**legacy docs/re/BKR2_RE_ANSWERS.md**

`Min_Speed` `PathFrameDelay` `Short_Range_Attack_Formation_Coefficient` `ShouldInfantryTeamsSplitAcrossFormations` `Solo_Attack_Range`

**legacy docs/re/BKT1_ATTACK_RE.md**

`Activation_Min_Range` `Custom_Soft_Footprint_Radius` `Is_Squashable` `Is_Squashable_By_Supercrusher` `Is_Supercrusher` `OccupationStyle` `Space_Obstacle_Radius` `UnitCollisionClass`

**legacy docs/re/BKX1_CADENCE_RE.md**

`Land_AI_Goal_Cycle_Sleep_Duration` `Space_AI_Goal_Cycle_Sleep_Duration`

**legacy docs/re/BKX1_RENDER_RE.md**

`Has_Pre_Turn_Anim` `Model_To_Attach` `Projectile_Disable_Engines_Duration` `SpaceIdleMovementSpeed`

**legacy docs/re/COMMANDS.md**

`Not_Really_Selectable` `Uses_Multiple_Locomotors`

**COMMAND_BAR_DRAG.md**

`Can_Drag_Stack` `Ghost_Base_Only` `Should_Ghost` `Should_Render_At_Drag_Pos` `Snap_Drag` `Snap_Location` `Stackable`

**legacy docs/re/CU8F_T01_T02_T07_RE.md**

`Catch_Dist` `Damage_Type` `Disable_Force_Abilities_Range` `Explode_When_Reached_Target_Radius` `Fires_Forward` `Holster_Disable_Engine_Loops` `Holster_Drawn_Bone_Translation` `Holster_Holstered_Bone_Translation` `Holster_Minimum_Drawn_Time_In_Secs` `Holster_Transition_Time_In_Secs` `Holster_Weapon_Bone_Name` `Projectile_Damage` `Projectile_Max_Flight_Distance` `Projectile_Object_Creation_Particle` `Projectile_Rocket_Curve_Distance` `Projectile_Rocket_Curve_Offset` `Projectile_Target_Point_On_Terrain` `SFXEvent_Draw_Weapon` `SFXEvent_Engine_Moving_Loop` `SFXEvent_Holster_Weapon` `Saber_Name` `SpaceBehavior`

**legacy docs/re/CU8F_T04_T06_RE.md**

`Base_Shield_Upgrade` `Causes_Infection` `Damage_Per_Second` `Disable_Reinforcement_Vulnerability` `Distance_To_Infect` `Duration_Of_Infection` `Enable_Bombing_Runs` `Enable_Planetary_Bombardment` `Enable_Special_Weapons` `Infection_CategoryMask` `Object_Max_Speed_Multiplier_Space` `Reverse_Application_Logic` `Seconds_To_Infect`

**legacy docs/re/CU8G_T02_ENV_PICK_RE.md**

`Emitter_Name` `Lightning_Intensity` `Map_Load_Spawn_Table`

**legacy docs/re/CU8K_T02_T06_RE.md**

`Garrison_Value` `MoveAttackWhileStopped`

**legacy docs/re/CU8K_T03_T08_T09_RE.md**

`Ship_Class` `Squadron_Offsets`

**legacy docs/re/DECORATIONS.md**

`Alpha` `Dense_FOW_Reveal_Range_Multiplier` `FOW_Reveal_Range_Multiplier` `Fadeout_Time` `LandFOWColor` `LandFOWRegrowTime` `Master_Decal_Slot_Count` `Permanent` `Random_Rotate` `Random_Size_Offset` `SetupPhaseFOWColor` `Slot_Scale` `SpaceFOWColor` `SpaceFOWHeight` `SpaceFOWRegrowTime` `SpaceReinforceFOWColor` `Texture_Name` `UV_Slot` `Width` `Z_Angle` `fade_begin_distance` `fade_distance_per_second` `fade_end_distance` `min_geometry_lod` `opacity` `segment_length` `texture_name` `width`

**legacy docs/re/FALLBACK_CAMPAIGNS.md**

`Destroyed_Galactic_Model_Name` `Galactic_Model_Name`

**legacy docs/re/FIGHTER_FLYING.md**

`Autonomous_Move_Extension_Vs_Attacker` `Avoidance_Disabled` `Bank_Turn_Angle` `Close_Enough_Angle_For_Move_Start` `ContainerArrangement` `Engines_Disabled_Speed_Modifier` `Fleeing_Infantry_Speed_Bonus` `Garrison_Bone_Names` `Garrison_Enter_Dist` `Garrison_Exit_Dist` `Garrison_Radius` `Keep_Moving_In_Battle` `LandObjectTrackingInterval` `LandObjectTrackingTreeCount` `Layer_Z_Adjust` `MaxJiggleDistance` `MaxWaypointsPerPath` `Max_Lift` `Max_Move_Frame_Delay` `Max_Rate_Of_Roll` `Max_Thrust` `Min_Speed_Fraction_For_Turn` `Minimum_Follow_Distance` `Num_Garrison_Slots` `Out_Of_Combat_Defense_Adjustment` `OverrideAcceleration` `OverrideDeceleration` `Prepare_Strafe_Height` `Scatters_From_Crushers` `Secondary_Locomotor_Name` `ShouldUseSpaceIdleMovement` `Spread_Out_Spacing_Coefficient` `Squadron_Formation_Error_Tolerance` `Stationary_Space_Layer` `Stopped_Rate_Of_Turn` `Strafe_Distance` `XYExpansionDistanceLand`

**legacy docs/re/FIXPASS_GALACTIC.md**

`Color_Zoomed_In` `Corruption_Line_Radius` `Corruption_Particle_Line_Name` `Corruption_Path_Color` `Corruption_Path_Offset` `Corruption_Path_Width` `Encyclopedia_Delay` `Evil_Side_Name` `Fleet_Hyperspace_Band_Texture_Name` `Fraction` `Good_Side_Name` `In_Background` `Is_Nebula` `Ranking_In_Category` `Reward_Param10` `Reward_Param9` `Tooltip_Delay`

**legacy docs/re/FOG_OF_WAR.md**

`Custom_Hard_XExtent_Offset` `Is_Asteroid_Field` `Is_Impassable_Asteroid` `Is_Ion_Storm` `Minimum_Tactical_Overrun_Time_In_Secs` `Modifies_Reveal_Range` `Multisample_FOW_Check` `Play_SFXEvent_On_Sighting` `Reveal_During_Setup_Phase_Only` `Reveal_For_Attacker` `Reveal_For_Defender` `Reveal_Range_Modifier` `SetupPhaseCountdownSeconds` `SetupPhaseInvalidDragColor` `Space_Obstacle_Offset` `Tactical_Overrun_Multiple` `Visible_On_Radar_When_Fogged`

**legacy docs/re/GALACTIC_DIALOGS.md**

`Battle_Pending_Message_Color` `Blank_Texture_Name` `Default_Transmission_Message` `Describe_Advantage` `Describe_History` `Describe_Population` `Describe_Tactical` `Describe_Terrain` `Describe_Weather` `Describe_Wildlife` `Encyclopedia_Weather_Icon` `GUI_Row` `Hidden` `Icon_Alternate_Texture_Name` `Model_Offset_Y` `Native_Icon_Name` `SFXEvent_Mission_Added` `Story_Dialog` `Story_Dialog_Popup` `Story_Dialog_SFX` `Story_Var` `Strategic_Command_Bar_Buildable_Row_Count` `Text_Color2`

**GALACTIC_ECONOMY.md**

`Black_Market_Income_Mult_Max` `Black_Market_Income_Mult_Min` `Blockade_Run_Attrition_Factor` `Can_Control_Planets` `Can_Halt_Credit_Production` `Credit_Cap_Per_Planet` `Duration_Of_Credit_Halt` `Force_Alignment` `Force_Strength` `Hyperspace_Speed` `Hyperspace_Speed_Factor` `MaxCreditIncomeAlignmentBonus` `MaxCreditIncomeAlignmentPenalty` `MaxInfluenceTransitionAlignmentBonus` `Object_Max_Speed_Multiplier_Galactic` `Piracy_Value_Credits` `Planet_Black_Market_Influences` `Planet_Credit_Value` `Planet_Destroyed_Credit_Value` `Political_Control_Change_Time_Seconds` `Prevents_Blockade_Run_Attrition` `Production_Speed_Mod_Base_Vs_Tech_1` `Squadron_Capacity` `Tactical_Build_Time_Bonus_Percentage` `The_Force`

**legacy docs/re/GALACTIC_PLANET_MAP.md**

`Base_Position` `Dialog_Scene` `Place_Other_Type_At_Every_Specific_Marker_Position` `Planet_Ability_Icon` `Planet_Groundbase_Stealth` `Tactical_Additional_Structure_Type`

**legacy docs/re/GALACTIC_REVEAL_CORRUPTION.md**

`Bribery_Fleet_Reveal_Range` `Can_See_Visibility_Level` `Corruption_Particle_Name` `Corruption_Removal_Cost_Multiplier` `Corruption_Removal_Time_In_Secs` `GUI_Planet_Fade_Duration` `Galactic_Influence_Range` `Hyperspace_Fleet_Reveal_Range` `Particle_Brightness_Per_Corruption_Level` `Particle_Energy_Per_Corruption_Level` `Particle_Scale_Per_Corruption_Level` `Planet_Corruption_Removal_Base_Cost` `Planet_Reveal_Delay_Time` `SFXEvent_Planet_Revealed`

**legacy docs/re/GALAXY_MAP_PRESENTATION.md**

`Blink_Duration` `Corruption_Planet_Icon` `Default_Offset` `Demo_Attract_Maps` `Galactic_Fleet_Override_Icon_Index` `Offset` `Reward_Param1` `Reward_Param2` `Scale` `Size` `Text_Offset2`

**legacy docs/re/GAME_SCORING.md**

`Game_Scoring_Script_Name`

**legacy docs/re/HARDPOINT_DISABLED.md**

`Allows_Special_Weapon_Use` `Depleted_Shield_Disable_Time` `Depleted_Shield_Regen_Cap` `Repair_Amount_Per_Frame` `Repair_Cost_Per_Frame` `SFXEvent_Starbase_Upgraded` `Shield_Refresh_Rate` `Special_Ability_Name`

**legacy docs/re/HERO_CLASH.md**

`Clash_Actions` `Clash_Range` `Clash_Type` `Combat_Distance` `Damage_Amount` `Damage_Percentage` `First_Hero_Damage_Multiplier` `First_Hero_Type` `First_Hero_Win_Exchange_Chance` `First_Hero_Win_Speech` `Friendly_Damage_Amount` `Involved_Hero_Types` `Play_Conversation_Events` `Projectile_Convert_Enemy_Targets_Category_Mask` `Second_Hero_Damage_Multiplier` `Second_Hero_Type` `Second_Hero_Win_Exchange_Chance` `Second_Hero_Win_Speech` `Vehicle_Thief_Inside_Clone`

**legacy docs/re/HOTSPOTS.md**

`Collision_Box_Modifier` `Fire_Range_Distance` `Surface_Type_Cover_Damage_Shield` `Tactical_Health`

**legacy docs/re/HUD_GAPS.md**

`Bombardment_Blob_Size` `Bombing_Run_Blob_Size` `Bombing_Run_Prevention_Radius` `Bombing_Run_Shadow_Blob_Material_Name` `Build_Texture_Name` `Disabled` `Encyclopedia_Fade_Rate` `Encyclopedia_Min_Display_Time` `Encyclopedia_Population_Offset` `Encyclopedia_Text` `HardPoint_Target_Reticle_Enemy_Screen_Size` `Is_Targetable` `Long_Encyclopedia_Delay` `Max_Bombing_Run_Interval_Seconds` `Mouse_Collide_Override_Sphere_Radius` `SFXEvent_Bomb_Run_Incoming` `SFXEvent_Bomb_Run_Select_Target` `SFXEvent_Bombing_Run_Cancelled` `Skirmish_Land_Bomber` `Tooltip_Text`

**legacy docs/re/L1P4_LUA_GAPS.md**

`Event_Param1`

**legacy docs/re/L1_S4_REMAINING.md**

`Affiliation` `Asteroid_Damage_Hit_Particles` `Asteroid_Field_Damage` `Asteroid_Field_Damage_Rate` `Begin_Control_Transition_Radar_Event` `Bombardment_Distribution` `Bombardment_Offset` `Bombardment_Particle` `Bombardment_Projectile` `Bombardment_Required_Orbital_Ships` `Bombing_Run_Percent_Modifier` `Bombing_Run_Reduction_Per_Squadron_Percent` `Bribe_Modifier_On_Uncorrupted_Planets` `Build_Can_Be_Unlocked_By_Slicer` `Capture_Point_Time_Multiplier` `Ease_Out_Duration` `Fog_Plane_Interpolation_Alpha` `Garrisoned_Max_Attack_Distance_Multiplier` `Hides_When_Built_On` `Land_Capture_Allowed_Countdown_Seconds` `Max_Bombard_Interval_Seconds` `Min_Bombard_Interval_Seconds` `Min_Bombing_Run_Interval_Seconds` `Modifies_Bombing_Run_Time` `Num_Garrison_Guns` `Projectile_Does_Shield_Damage` `Random_Story_Triggers` `Respects_Skirmish_Pre_Build_Bases_Option` `SFXEvent_Bombard_Available` `SFXEvent_Bombing_Run_Begin_Crosstalk` `SFXEvent_Bunker_Garrisoned` `SFXEvent_Damaged_By_Asteroid` `SFXEvent_HUD_Reinforcement_Point_Contested` `SFXEvent_Tactical_Build_Complete` `SFXEvent_Tactical_Build_Started` `Space_Capture_Allowed_Countdown_Seconds` `Tactical_Bribe_Cost` `Tactical_Buildable_Objects_Campaign` `Tactical_Buildable_Objects_Multiplayer` `Tactical_Sell_Percentage` `Visible_To_Enemies_When_Empty`

**legacy docs/re/LOB_SOLVER.md**

`Bomb_Type` `Fire_Projectile_Type` `Fire_Weapon_When_Undeployed` `Grenade_Explode_Timer_In_Secs` `Grenade_Type` `Land_Damage_Alternates` `Land_Damage_Thresholds` `Lobbing_Superweapon_Chargeup_Frames` `Muzzle_Bone_Name` `Projectile_Grenade_Can_Lob_Slower` `Projectile_Grenade_Gravity` `Projectile_Grenade_Gravity_Lob_Mod` `Projectile_Grenade_Sticks_On_Collision` `Projectile_Max_Lifetime` `Spawn_Bone` `Turret_Bone_Name` `Turret_Deployed_Rest_Angle` `Turret_Elevate_Extent_Degrees` `Turret_Rest_Angle` `Turret_Rotate_Extent_Degrees` `Turret_Rotate_Speed` `Turret_XY_Only`

**legacy docs/re/LUA_API.md**

`Prereq`

**legacy docs/re/MAIN_LOOP.md**

`Demo_Attract_Start_Timeout_Seconds`

**legacy docs/re/MAIN_MENU_BACKGROUND.md**

`Demo_Attract_Map_Cycle_Delay_Seconds` `Main_Menu_Demo_Attract_Mode` `Multiplayer` `Tutorial`

**legacy docs/re/MAP1_RADAR_TERRAIN_RE.md**

`Land_Backdrop_Texture_Name` `Land_FOW_Color`

**legacy docs/re/MCX0_T07_UNLISTED_TAGS.md**

`Special_Case_Production`

**legacy docs/re/MCX1_RE_ANSWERS.md**

`Galactic_Position`

**legacy docs/re/MCX4_GAL2_RE_ANSWERS.md**

`Corruption_1_Type` `Corruption_Level_Override` `Default_Hero_Respawn_Time` `Enemy_Spawn_Text` `Friendly_Spawn_Text` `Indigenous_Spawn_Destruction_Reward` `Respawn_Whole_Team_When_Killed` `Squadron_Refill_Threshold`

**legacy docs/re/MCX4_PRES_RE_ANSWERS_2.md**

`Alternate_Description_Text` `Alternate_Name_Text` `Auto_Destruct_Fade_Time` `Auto_Destruct_Time` `Blob_Radius` `Build_Countdown_Text_ID` `Build_Countdown_Text_RGBA` `Diffuse` `Enemy_Color` `Falloff_Start` `GUI_Strategic_Countdown_Timers_Screen_Spacing` `GUI_Strategic_Countdown_Timers_Screen_X` `GUI_Strategic_Countdown_Timers_Screen_Y` `GUI_Tactical_Countdown_Timers_Screen_X` `GUI_Tactical_Countdown_Timers_Screen_Y` `Game_Play_UI_Countdown_Font_Name` `Game_Play_UI_Countdown_Font_Size` `Highlight_Blob_Material_Name` `Land_Is_Guide_Rectangle` `Land_Retreat_Cancel_SFXEvent` `Land_Retreat_Countdown_Color_RGBA` `Land_Retreat_Countdown_Seconds` `Land_Retreat_Countdown_Text_ID` `Land_Terrain_Model_Mapping` `Num_Slots` `Particle_Attach_To_Collision` `Player_Color` `Projectile_Object_Detonation_Particle` `Replace_Faded` `SFXEvent_Retreat_Cancel` `Scale_Duration` `Selection_Blob_RGBA` `Shield_Hit_Particles` `Slot_X` `Slot_Y` `Space_Is_Guide_Rectangle` `Space_Retreat_Cancel_SFXEvent` `Space_Retreat_Countdown_Color_RGBA` `Space_Retreat_Countdown_Text_ID` `Texture_Repeat` `Texture_Scroll` `Use_Event_System`

**legacy docs/re/MCX4_PRES_RE_ANSWERS_3.md**

`Alternate_Font_Name` `Camera_Aligned` `Font_Name` `Font_Point_Size` `GUI_Flash_Duration` `GUI_Flash_Level` `GUI_Rapid_Flash_Duration` `Icons_Per_Column` `Intensity` `Min_Accuracy_For_Icon` `Min_Sight_Range_For_Icon` `Minimum_Visible_Base_Level` `Movie_Start_Frame` `Overlay` `Pause_Game_While_Playing` `Radar_Icon_Size` `Radar_Rotate_Icon` `Radar_Show_Facing` `SFXEvent_Bombard_Incoming` `Should_Death_Clone_Play_Idle` `Show_In_Sidebar_Tray` `Show_In_Sidebar_When_Complete` `Special_Weapon_Index` `Standalone_Space_Maps_Special_Weapon_A` `Tool_Tip_Small_Font_Name` `User_Bound_Min`

**legacy docs/re/MCX4_RE_ANSWERS_GALACTIC.md**

`Bribe_Cost_Multiplier` `Build_Requires_Initial_Placement` `Campaign_Set` `Corruption_Tactical_Build_Pad_Price_Multipliers` `Corruption_Tactical_Production_Price_Multipliers` `Corruption_Tactical_Production_Time_Multipliers` `Diminishing_Firepower` `Easily_Bribed` `Is_Listed` `Is_Story_Campaign` `Random_Story_Max_Triggers` `Random_Story_Rewards` `Show_Completed_Tab`

**legacy docs/re/MCX4_RE_ANSWERS_TACTICAL.md**

`Absorb_Chance` `Build_Limit_Lifetime_For_All_Allies` `Cash_Point_Credits_Per_Second` `Creation_Delay_In_Seconds` `Damage_Absorb_Amount` `Damage_Absorb_Percentage` `Explosion_Type` `Influences_Cash_Point` `Is_Indigenous_Unit_Corruptible` `Projectile_Blast_Area_Dropoff` `Projectile_Bomb_Fall_Accel_Rate` `Recharge_Time_In_Seconds` `Respawn_Tied_To_Structure` `Spawn_Bone_Name` `Tactical_Build_Increments_Tech_Level` `Tactical_Destruction_Decrements_Tech_Level`

**legacy docs/re/MCX4_RE_QUESTIONS_TACTICAL.md**

`Affects_All_Allied_Sources` `Can_Be_Only_One` `Cash_Point_Radius` `Cash_Point_Transition_Time_Seconds` `Evade_Detection_Chance` `Interval_Multiplier` `Projectile_Blast_Area_Dropoff_Tiers` `Projectile_Max_Scan_Range` `Split_Income_With_Allies` `Unit_Strength_Category` `Unit_Strength_Factor`

**legacy docs/re/MCX4_T09_RE_ANSWERS.md**

`Activated_Black_Market_Ability_Names` `Activated_Corrupt_Planet_Ability_Names` `Activated_Destroy_Planet_Ability_Names` `Activated_Hack_Super_Weapon_Ability_Names` `Activated_Neutralize_Hero_Ability_Names` `Activated_Remove_Corruption_Ability_Names` `Activated_Sabotage_Ability_Names` `Activated_Siphon_Credits_Ability_Names` `Activated_Slice_Ability_Names` `Activated_System_Spy_Ability_Names` `Base_Income_Value` `Base_Interval_In_Secs` `Build_Countdown_Timer` `Build_Speech_Completed` `Build_Speech_Countdowns` `Build_Speech_Stopped` `Build_Speech_Underway` `Corruption_1_Success_Credit_Bonus` `Corruption_2_Success_Credit_Bonus` `Corruption_3_Success_Credit_Bonus` `Damage` `Detect_Credit_Siphon_Effects` `Detection_Chance` `Full_Amount_To_Everyone` `Income_Additive_Value` `Income_Multiplier` `Min_Siphon_Percentage` `Owner_Income_Percentage` `Split_Favors_Owner` `Target_Stream_Source`

**legacy docs/re/MCX4_T10_RE_ANSWERS.md**

`Action_Object` `Action_Relevance_Constant` `Action_Relevance_Type` `Action_Target` `Activate_SFX` `Activation_Time_In_Seconds` `Allow_Opportunity_Fire_When_Targeting` `Attack_Animation_Type` `Attack_Firing_Bone_Name` `Attack_Number_Of_Shots` `Attack_Projectile_Type` `Attack_SFXEvent` `Can_Contain_Heroes_During_Ground_Battle` `Chase_Radius` `Conversion_Ability_Changes_To_Enemy` `Damage_Arc_In_Degrees` `Damage_Delay_In_Secs` `Damage_Radius` `Depleted_Shield_Damage_Increment` `Deployed_Max_Attack_Distance_Multiplier` `Deployed_Turret_Elevate_Extent_Degrees` `Elevated_Vulnerability_Duration` `Elevated_Vulnerability_Factor` `Enemy_Damage_Per_Second` `Engine_Death_Hide_Engine_Particles` `Fire_Time_In_Secs` `Fire_When_In_Power_To_Weapons_Mode` `Full_Salvo_Weapon_Delay_Multiplier` `Inaccuracy_Radius` `Is_Force_Sensitive` `Manual_Hardpoint_Firing_Cooldown_Secs` `Override_Death_Persistence_Duration` `Own_Damage_Per_Second` `Projectile_Block_Chance_Modifier` `Projectile_Fire_Pulse_Count` `Projectile_Redirect_Chance_Modifier` `Shield_Drain_Per_Second` `Space_Elevated_Vulnerability_Duration` `Space_Elevated_Vulnerability_Factor` `Space_Forced_Retreat_Due_To_Superweapon` `Special_Weapon_Valid_Targets` `Team_Member_With_Ability` `Terminate_Effect_On_Move_Command` `Turn_To_Face_Unblockable_Shots`

**GALACTIC_PAUSE.md**

`Animate_During_Galactic_Mode_Pause`

**legacy docs/re/MCX4_T11_T12_RE_ANSWERS.md**

`AI_BuildTaskReservationSeconds` `Ability_Cooldown_Time_In_Secs` `Ability_Name` `ApproximationSmoothCosAngle` `Attach_To_Flagship_During_Space_Battle` `Avoid_Enemy_Exclusion_Range` `Burning_Damage_Per_Second` `Can_Indigenous_Unit_Stop` `CrouchIdleWalkBlendTime` `Crouch_Move_Fire_Angle_Cutoff` `Exit_Door_Angle_Degrees` `Exit_Door_Distance` `FinalFormationFacingDeltaCoefficient` `FinalFormationFacingMinimumAngle` `FormationMaximumSideError` `FormationMinimumSideError` `GripperCombatGridSnapDistance` `Harass_Enemy_Exclusion_Range` `IdleMovementFrames` `IdleWalkBlendTime` `Ignore_For_Reoptimization` `LateralAcceleration` `MaxFacingLookAheadFrames` `Max_Distance_From_Spawner` `Move_Animation_Override` `Move_Animation_Speedup` `Move_Animation_Subindex` `Move_Animation_Type` `Move_Max_Distance` `Move_Min_Distance` `Move_Speed` `Move_Type` `Move_Use_Jetpack` `MovementReevaluationFrameCount` `Rotate_Formation_Facing_Moves` `Saliency_Size` `ShouldSkipLandFormup` `Snap_Movement_Orders_To_Center` `SpaceLocomotorFacingLookaheadAcc` `SpaceObjectTrackingInterval` `SpaceStaticObstacleAvoidanceBonusDistance` `TeamCrouchMoveBlendTime` `TeamMoveBlendTime` `Unit_Command_Rankings_By_Category` `Wait_For_Finish_If_Target_Using_Jetpack`

**legacy docs/re/MCX4_T13_RE_ANSWERS.md**

`Corruption_1_Failure_Speech_Event` `Corruption_1_Mission_Commencing_Speech_Event` `Corruption_1_Success_Speech_Event` `Corruption_2_Failure_Speech_Event` `Corruption_2_Success_Speech_Event` `Corruption_3_Failure_Speech_Event` `Corruption_3_Success_Speech_Event` `Death_Explosion_SFXEvent` `Health_Critical_Percent_Threshold` `Land_Damage_SFX` `Land_Retreat_Begin_SFXEvent` `Land_Retreat_Enemy_Begin_SFXEvent` `Land_Retreat_Not_Allowed_Reason_1_SFXEvent` `Land_Retreat_Not_Allowed_Reason_2_SFXEvent` `Land_Retreat_Not_Allowed_Reason_3_SFXEvent` `Land_Retreat_Not_Allowed_SFXEvent` `Mine_Type` `MusicEvent_Frame` `Music_Event_Battle_End_Summary_Screen_Win` `Music_Event_Tactical_Lose` `Music_Event_Tactical_Lose_Vs_Faction` `Music_Event_Tactical_Win` `Number_Of_Mines` `Reinforcements_Cancelled_SFXEvent` `Reinforcements_Enroute_SFXEvent` `Reinforcements_Pick_Landing_Zone_SFXEvent` `Reinforcements_Ready_SFXEvent` `Reinforcements_Selection_SFXEvent` `SFXEvent_Assist_Attack` `SFXEvent_Attack_Hardpoint` `SFXEvent_Base_Shield_Absorb_Damage` `SFXEvent_Bombard_Ally_Available` `SFXEvent_Bombard_Cancelled` `SFXEvent_Bombard_Enemy_Available` `SFXEvent_Bombing_Run_Ally_Available` `SFXEvent_Bombing_Run_Available` `SFXEvent_Bombing_Run_Enemy_Available` `SFXEvent_Build_Cancelled` `SFXEvent_Bunker_Vacated` `SFXEvent_Cough_Override` `SFXEvent_Enemy_Damaged_Health_Critical_Warning` `SFXEvent_Enemy_Damaged_Health_Low_Warning` `SFXEvent_Enemy_Spotted` `SFXEvent_Fail` `SFXEvent_Fleet_Move` `SFXEvent_Frame` `SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_Off` `SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_On` `SFXEvent_GUI_Toggle_Non_Hero_Ability_Off` `SFXEvent_GUI_Toggle_Non_Hero_Ability_On` `SFXEvent_GUI_Unit_Ability_Deactivated` `SFXEvent_Generic_Unit_Spotted` `SFXEvent_Group_Attack` `SFXEvent_HUD_Advisor_Hint` `SFXEvent_HUD_Advisor_Message` `SFXEvent_HUD_Base_Shield_Offline` `SFXEvent_HUD_Base_Shield_Online` `SFXEvent_HUD_Enemy_Base_Shield_Offline` `SFXEvent_HUD_Enemy_Base_Shield_Online` `SFXEvent_HUD_Enemy_Special_Weapon_Firing` `SFXEvent_HUD_Enemy_Special_Weapon_Ready` `SFXEvent_HUD_Gravity_Control_Generator_Off` `SFXEvent_HUD_Gravity_Control_Generator_On` `SFXEvent_HUD_Lost_Land_Battle` `SFXEvent_HUD_Lost_Land_Battle_Enemy_TSW_Present` `SFXEvent_HUD_Lost_Space_Battle` `SFXEvent_HUD_Lost_Space_Battle_Enemy_TSW_Present` `SFXEvent_HUD_Lost_Tactical_Corruption_Mission` `SFXEvent_HUD_Repairing` `SFXEvent_HUD_Special_Weapon_Firing` `SFXEvent_HUD_Special_Weapon_Ready` `SFXEvent_HUD_Won_Land_Battle` `SFXEvent_HUD_Won_Land_Battle_Enemy_TSW_Present` `SFXEvent_HUD_Won_Space_Battle` `SFXEvent_HUD_Won_Space_Battle_Enemy_TSW_Present` `SFXEvent_HUD_Won_Tactical_Corruption_Mission` `SFXEvent_Hardpoint_All_Weapons_Destroyed` `SFXEvent_Hardpoint_Destroyed` `SFXEvent_Health_Critical_Warning` `SFXEvent_Health_Low_Warning` `SFXEvent_Move_Into_Asteroid_Field` `SFXEvent_Move_Into_Nebula` `SFXEvent_Powered_Active_Loop` `SFXEvent_Radioactive` `SFXEvent_Retreat_Start` `SFXEvent_Sold_Tactical` `SFXEvent_Special_Weapon_Ready` `SFXEvent_Success` `SFXEvent_Tactical_Build_Cancelled` `SFXEvent_Tactical_Gain_Enemy_Control` `SFXEvent_Tactical_Gain_Friendly_Control` `SFXEvent_Tactical_Lose_Enemy_Control` `SFXEvent_Tactical_Lose_Friendly_Control` `SFXEvent_Tactical_Object_Building_Complete` `SFXEvent_Tactical_Object_Building_Loop` `SFXEvent_Tactical_Object_Building_Started` `SFXEvent_Tactical_Object_Sold` `SFXEvent_Tactical_Pop_Cap_Reached` `SFXEvent_Tactical_Unit_Cap_Reached` `SFXEvent_Turret_Rotating_Loop` `SFXEvent_Unit_Summoned` `SFXEvent_Unit_Type_Spotted` `Selt_Destruct_SFXEvent_Start_Die` `Space_Retreat_Enemy_Begin_SFXEvent` `Space_Retreat_Not_Allowed_Reason_1_SFXEvent` `Space_Retreat_Not_Allowed_Reason_2_SFXEvent` `Space_Retreat_Not_Allowed_Reason_3_SFXEvent` `Space_Retreat_Not_Allowed_SFXEvent` `Special_Weapon_Target_Action_Index` `SpeechEvent_Frame` `SpeechEvent_Super_Weapon_Enemy_Moved_Into_Range` `SpeechEvent_Super_Weapon_Enemy_Moving_Range_05_Seconds` `SpeechEvent_Super_Weapon_Enemy_Moving_Range_15_Seconds` `SpeechEvent_Super_Weapon_Enemy_Moving_Range_30_Seconds` `SpeechEvent_Super_Weapon_Enemy_Moving_Range_60_Seconds` `SpeechEvent_Super_Weapon_Moved_Into_Range` `SpeechEvent_Super_Weapon_Moving_Range_05_Seconds` `SpeechEvent_Super_Weapon_Moving_Range_15_Seconds` `SpeechEvent_Super_Weapon_Moving_Range_30_Seconds` `SpeechEvent_Super_Weapon_Moving_Range_60_Seconds` `SpeechEvent_Tactical_Intro_Land_Attacker` `SpeechEvent_Tactical_Intro_Land_Defender` `SpeechEvent_Tactical_Intro_Land_Raid_Attacker` `SpeechEvent_Tactical_Intro_Land_Raid_Defender` `SpeechEvent_Tactical_Intro_Space_Attacker` `SpeechEvent_Tactical_Intro_Space_Defender` `Speech_Text_Color` `Spin_Away_On_Death_SFXEvent_Start_Die` `TSW_Attack_Distance_From_Target` `TSW_SFXEvent_GUI_Bttton_Press` `TSW_SFXEvent_Weapon_Power_Up` `Trigger_Radius` `Zap_SFXEvent`

**legacy docs/re/MCX4_T14_RE_ANSWERS.md**

`Alternate_Icon_Prefix` `Auto_Rotate_For_Space_Targeting` `Battle_Load_Planet_Ambient` `Battle_Load_Planet_Direction` `Battle_Load_Planet_Viewport` `Beacon_Lifetime_In_Secs` `Beacon_Radar_Map_Event_Name` `CB_Flash_Count` `Can_Drag_Stack` `Command_Bar_Default_Font_Name` `Command_Bar_Default_Font_Size` `Corrupt_Side_Leader_Name` `Corrupt_Side_Name` `Corruption_1_Failure_Bink_Hologram_Name` `Corruption_1_Mission_Briefing` `Corruption_2_Failure_Bink_Hologram_Name` `Corruption_2_Mission_Briefing` `Corruption_3_Failure_Bink_Hologram_Name` `Corruption_3_Mission_Briefing` `Corruption_Choice_Benefit` `Corruption_Choice_Encyclopedia` `Corruption_Choice_Icon_Name` `Corruption_Choice_Name` `Corruption_Encyclopedia_Backdrop` `Corruption_Encyclopedia_Complete` `Corruption_Encyclopedia_Header` `Corruption_Encyclopedia_Incomplete` `Corruption_Encyclopedia_Left_Edge` `Corruption_Encyclopedia_Money_Icon` `Corruption_Encyclopedia_Spacing` `Corruption_Line_Grow_Seconds` `Corruption_Line_Start_End_Offset` `Corruption_Mission_Requirement_Icon_Name` `Corruption_Planet_Icon_Encyclopedia_Desc` `Corruption_Planet_Icon_Encyclopedia_Name` `Cross_Fade` `Defeat_Text` `Droid_Encyclopedia_Offset` `Encyclopedia_Weather_Info` `Encyclopedia_Weather_Name` `End_Control_Transition_Radar_Event` `Evil_Side_Leader_Name` `Fighter_Icon_Name` `Finale_Movie` `Finale_Movie_2` `GMC_Battle_Fade_Time` `GMC_Battle_Zoom_Time` `GUI_Cycle_Color` `GUI_Cycle_Speed` `GUI_Darken_Level` `Galactic_Zoom_In_Station_Rotation` `Game_Object_Name_Font_Name` `Game_Object_Name_Font_Size` `Generic_Win_Movie` `Ghost_Base_Only` `Good_Side_Leader_Name` `Helper_Icon_Name` `Hero_Ability` `Infantry_Icon_Name` `Land_Ability_Targeting_Range_Overlay_Material_Name` `Land_Ability_Targeting_Range_Overlay_Scale_Factor` `Land_Area_Effect_Range_Overlay_Material_Name` `Land_Area_Effect_Range_Overlay_Scale_Factor` `Land_Lose_Image` `Land_Win_Image` `Left_Queue_Tint` `Lose_Message_Color` `MP_Encyclopedia_Text` `Mouse_Over_Highlight_Scale` `Multiplayer_Losing_Team_Bonus_Credit_Percentage` `Num_Structures_For_Large_Planet_Name` `Num_Structures_For_Medium_Planet_Name` `Offset_Render` `Planet_Ability_Icon_Names` `Planet_Ability_RGBs` `Planet_Ability_Text_IDs` `PlayModeSwitchMovies` `Post_Credits_Movie` `Radar_Clip_To_Visible_Region` `Radar_Icon_Scale_Land` `Radar_Icon_Scale_Space` `Radar_Range_Icon_Name` `Radar_Register_As_Foreground_Object` `Right_Queue_Tint` `Secondary_Objective` `Shield_Flash_Duration` `Shield_Flash_Scale` `Shield_Sub_Mesh` `ShipNameTextFiles` `Should_Ghost` `Show_Hero_Head` `Show_Name` `Snap_Drag` `Snap_Location` `SpaceReinforceFeedbackOnlyWhileDragging` `Space_Lose_Image` `Space_Win_Image` `Stackable` `Superweapon_Win_Movie` `Swap_Texture` `Team_Healthbar_Offset` `Text_Button_Default_Font_Name` `Text_Button_Default_Font_Size` `Text_Reveal_Rate` `Tool_Tip_Font_Name` `Tool_Tip_Font_Size` `Tool_Tip_Small_Font_Size` `Victory_Text` `WaypointFlagModelName` `WaypointLineLandDashLength` `WaypointLineLandDashVelocity` `WaypointLineLandGapLength` `WaypointLineTextureName` `Win_Lose_Message_Font` `Win_Lose_Message_Font_Size` `Win_Message_Color`

**legacy docs/re/MCX4_T15_RE_ANSWERS.md**

`Barrel_Bone_Name` `Bombardment_Lighting_Color_List` `Bombardment_Lighting_Intensity_List` `Bombardment_Lighting_Seconds_List` `Bombardment_Shadow_Blob_Material_Name` `Bribed_Color` `Bribed_Effect` `Bribed_Effect_Small` `CB_Movie_Color` `Cable_End_Speed_Fraction` `Cable_Length_Fraction` `Cable_Render_Mode` `Cable_Texture_Name` `Cable_Width` `Charged_Effect` `Charging_Effect` `Cinematic_Anim_Index` `Death_Explosions_End` `Dynamic_Transform_Delay_Seconds` `Dynamic_Transform_End_Color` `Dynamic_Transform_End_Scale` `Dynamic_Transform_Seconds` `Dynamic_Transform_Start_Color` `Dynamic_Transform_Start_Scale` `Earthquake_Shake_Magnitude` `Earthquake_Shake_Speed` `Earthquake_Transition_Time` `Effect_Z_Offset` `Exclude_From_Distance_Fade` `Flame_Emitter_Model_Name` `Fleet_Movement_Line_Texture_Name` `Glory_Cinematics` `Good_Ground_Color_Tint` `Grenade_Spawn_Bone` `High_Ground_Color_Tint` `Hover_Height` `Infantry_Ground_Color_Tint` `Land_Ability_Targeting_Range_Overlay_RGBA` `Land_Area_Effect_Range_Overlay_RGBA` `Land_Collidable_Grid_Cull_Size` `Land_Tactical_Camera_Locked` `Lava_Ground_Color_Tint` `Leech_Effect` `Lightning_Source_Bone_Name` `Lightning_Target_Bone_Name` `Nebula_Effect_Color` `Neutral_UI_Color` `Occlusion_Silhouette_Enabled` `Occlusion_Silhouettes_Enabled` `Owner_Light_Effect_Color` `Owner_Light_Effect_Color2` `Owner_Light_Effect_Duration` `Owner_Light_Effect_Pulse_Count` `Owner_Light_Effect_Type` `Pause_During_Cinematic_Anim` `Projectile_Cause_Invulnerability_Spawn_Effect` `Projectile_Combat_Mod_Effect` `Projectile_Convert_Enemy_Spawn_Effect` `Projectile_Instant_Heal_Spawn_Effect` `Projectile_Weaken_Enemy_Spawn_Effect` `ReinforcementOverlayBadColor` `ReinforcementOverlayGoodColor` `Reinforcement_Region_Blob_Name` `Retreat_Self_Destruct_Explosion` `Slow_Ground_Color_Tint` `Space_Collidable_Grid_Cull_Size` `Space_Large_Ship_Grid_Cull_Size` `Space_Tactical_Camera_Locked` `Start_Lightning_Frame_Number` `Target_Light_Effect_Color` `Target_Light_Effect_Color2` `Target_Light_Effect_Duration` `Target_Light_Effect_Pulse_Count` `Target_Light_Effect_Type` `Telekinesis_Hover_Height` `Telekinesis_Max_Bob_Height` `Telekinesis_Max_Wobble_Angle` `Telekinesis_Transition_Time` `Telekinesis_Wobble_Cycle_Time` `Telekinesis_Wobble_Fade_Time` `Terrain_Resurface_Rand` `Terrain_Resurface_Tolerance` `Terrain_Texture_Modifier_Join_Distance` `Terrain_Texture_Modifier_Material` `Tread_Scroll_Rate` `Use_Neutral_UI_Color` `Wind_Disturbance_Radius` `Wind_Disturbance_Sphere_Alpha` `Wind_Disturbance_Strength` `Zoomed_Terrain_Index`

**legacy docs/re/MCX5_RE_ANSWERS.md**

`Create_Player_In_Multiplayer_Games` `Displayed_Tech_Level_Adjustment` `MP_Default_Max_Tech_Level` `MP_Default_Start_Tech_Level` `Max_Custom_Starting_Credits` `Max_Custom_Tech_Level` `Max_Respawn_Time_Per_Tech_Level` `Min_Custom_Tech_Level` `Min_Respawn_Time_Per_Tech_Level` `Starting_Tech_Level` `Supports_Custom_Settings`

**legacy docs/re/MCX6_T05_CINEMATIC_BINDINGS.md**

`SFXEvent_Ambient_Moving`

**legacy docs/re/MODC_LOADER.md**

`Projectile_Stun_Victims_Category_Mask`

**MOVEMENT.md**

`Alternate_Max_Rate_Of_Turn` `Alternate_Max_Speed` `Base_Land_Targeting_Arc_Angle_Coefficient` `BetweenFormationSpacing` `Cache_Crusher_Boxes` `CloseEnoughAngleForMoveStart` `Custom_Hard_YExtent_Offset` `Death_Clone_Is_Obstacle` `Deployed_Turret_Rotate_Extent_Degrees` `Deployed_Walk_Speed` `Destination_Collision_Query_Extension` `DoubleClickMoveMaxSpeedRatio` `DynamicAvoidanceRectangleBound` `DynamicLandComplexityQuota` `DynamicObstacleOverlapPenalty` `Fine_Tune_Occupied_Passability` `Fire_Weapon_When_Deployed` `Fire_When_Deployed` `FormationGrouping` `FormationOrder` `FormationRaggedness` `Formation_Formup_Wait_Style` `InfantryFormationRecruitmentDistance` `IsDeathCloneObstacle` `Is_Stationary_When_Attacking` `LandTemporaryDestinationProximity` `Mass` `MaxSecondaryTurnROTCoefficient` `Melee_Cutoff_Range` `MinSecondaryTurnROTCoefficient` `MinimumStoppedVsStoppedOverlapCoefficient` `Movement_Directions_Count` `Movement_Speed_Bonus_Percentage` `MovingVsMovingLookAheadTime` `Obstacle_Height` `Obstacle_Proxy_Type` `Obstacle_Width` `Obstacle_X_Offset` `Obstacle_Y_Offset` `On_Fire_Speed_Modifier` `Overall_Length` `Overall_Width` `OverridePassability` `Primary_Locomotor_Name` `Rotation_Animation_Speed` `SecondaryOccupationPassability` `SecondaryTurnAngle` `SecondaryTurnInPlaceROTCoefficient` `SecondaryTurnLookaheadDistance` `Shielded_Only_When_Deployed` `Squash_Damage_Type` `Turret_Rotate_To_Rest_Delay_Sec` `UseSecondaryFacing` `Use_Special_Submit_Rules` `VehicleFormationRecruitmentDistance` `Walker_Acceleration_Magic_Number`

**legacy docs/re/OBJECT_SCRIPTS.md**

`Model_Visible_To_Enemy`

**legacy docs/re/OBJECT_TYPES.md**

`Blob_Shadow_Material_Name` `Bomb_Damage_Type` `Build_Max_Instances_Per_Planet` `Can_Hyperspace_Without_Activating_Ability` `Category_Exclusions` `Death_Explosions` `Death_Leave_Hulk_Behind` `Destroy_Previous_Upgrade_Level` `Destroy_When_Used_As_Corruption_Prereq` `Display_Contained_Hero_Grab_Bars` `Excluded_Unit_Categories` `Explosion_Scorch_Mark` `Fire_Category_Restrictions` `Fire_Min_Range_Distance` `Fire_Weapon_When_In_Normal_Attack_Mode` `Fire_Weapon_When_In_Rocket_Attack_Mode` `Galactic_Fleet_Override_Model_Name` `Internal_Damage_Type` `Is_Branched_Map_Discardable` `Is_Discardable` `Is_Editor_Placed` `Is_Homogeneous` `Is_Sellable` `Is_Visible_On_Enemy_Radar` `LOD_Bias` `Movie_Object` `Next_Upgrade_Level_Type` `Planet_Is_Destroyable_By_TSW` `Planet_Slice_Affinity_Types` `Prev_Level_Base` `Previous_Upgrade_Level_Type` `Priority_Attack_HardPoints` `Projectile_Absorbed_By_Shields_Particle` `Projectile_Combat_Mod_Category_Mask` `Projectile_Does_Hitpoint_Damage` `Projectile_Ground_Detonation_SurfaceFX` `Property_Exclusions` `Property_Flags` `Radar_Icon_Name` `SFXEvent_Attack_Override` `Score_Cost_Credits` `Shield_Armor_Type` `Shield_Points` `Shield_Requires_Base_Power` `Should_Cause_Limited_Turrets_To_Turn` `Should_Reinforcements_Move` `Show_In_Sidebar_While_Building` `SurfaceFX_Name` `TSW_Start_Pos_Match_Targets_Z` `Targeting_Land_Model_Stay_Horiz_Flat` `Terrain_Texture_Modifier_Square` `Use_Transported_Object_Bounds_For_Landing_Z`

**legacy docs/re/P1_CAMERA_INPUT.md**

`Distance_Per_Mouse_Unit` `Distance_Smooth_Time` `Fov_Per_Mouse_Unit` `Fov_Smooth_Time` `GMC_InitialPitchAngleDegrees` `GMC_InitialPullbackDistance` `GMC_ZoomTime` `GMC_ZoomedPitchAngleDegrees` `GMC_ZoomedPositionOffsetPlanetRadiusFractions` `GMC_ZoomedPullbackPlanetRadiusFraction` `Galactic_Right_Button_Scroll_Speed_Factor` `Galactic_Scroll_Plane` `Galactic_Zoom_Light_Level` `Galactic_Zoom_Out_Light_Angle` `Location_Follows_Terrain` `Location_Height_Up_Smooth_Time` `Min_Height_Above_Terrain` `Pitch_Per_Mouse_Unit` `Pitch_Per_Zoom_Unit` `Pitch_When_Zoomed_In` `Pitch_Zoom_Begin_Fraction` `Push_Scroll_Speed_Modifier` `Scroll_Acceleration_Factor` `Scroll_Deceleration_Factor` `Spline_Smooth_Time` `Spline_Steps` `Tactical_Edge_Scroll_Region` `Use_Splines` `Yaw_Per_Mouse_Unit`

**legacy docs/re/P1_DOUBLE_CLICK_MOVE.md**

`GUI_Attack_Move_Command_Ack_Effect` `GUI_Attack_Movement_Click_Radar_Event_Name` `GUI_Double_Click_Move_Command_Ack_Effect` `GUI_Guard_Move_Command_Ack_Effect` `GUI_Move_Acknowledge_Scale_Land` `GUI_Move_Command_Ack_Effect` `GUI_Movement_Click_Radar_Event_Name` `GUI_Movement_Double_Click_Radar_Event_Name`

**legacy docs/re/P4_SIM_SYSTEMS.md**

`Ability_Names` `Absolute_Income_Modifier` `Additional_Multiplier_On_Mining_Colonies` `Advisor_Hint_Duration` `Base_Cost_Credits` `Can_Cancel_Production` `Can_Destroy_Special_Structure` `Can_Reduce_Ground_Base_Level` `Can_Reduce_Starbase_Level` `Causes_Despawn` `Chance_To_Win` `Color` `Droid_Date_Color` `Droid_Seperator_Color` `Evaluation_Interval_In_Secs` `Hack_Super_Weapon_Cost` `Hack_Super_Weapon_Particle_Effect` `Hack_Super_Weapon_Required_Type` `Localized_Name` `Max_Income_Modifier` `Max_Respawn_Times` `Min_Income_Modifier` `Min_Respawn_Times` `Percentage_Income_Modifier` `Planet_Black_Market_Modifier` `Planet_Is_Mining_Colony` `Planet_Vigilance` `Price_Modifier` `SFXEvent_Enemy_Fleet_Approaching_Planet` `SFXEvent_GUI_Start_Campaign` `SFXEvent_Hack_Success` `SFXEvent_Hero_Respawned` `SFXEvent_New_Construction_Options_Available` `SFXEvent_Planet_Corrupted` `SFXEvent_Planet_Gained_Control` `SFXEvent_Slice_Success` `SFXEvent_Strategic_Pop_Cap_Reached` `Sabotage_Cost_Credits` `Sabotage_Cost_Increase_Factor` `Sabotage_Particle_Effect` `Specific_Mod_Source_Text` `Starting_Active_Player` `Tech_Level_Adjustment` `Tutorial_Base_Cost_Credits` `Tutorial_Sabotage_Cost_Credits` `Tutorial_Tech_Level` `Uncommon_Price_Multiplier` `Used_By_Units`

**legacy docs/re/PROJECTILE_NEBULA_DRAW.md**

`Laser_Beam_Z_Scale_Factor` `Laser_Kite_Z_Scale_Factor` `Projectile_Laser_Color` `Projectile_Length` `Projectile_Texture_Slot` `Projectile_Width`

**legacy docs/re/Q6_T15_RETAIL_RUN.md**

`AI_Victory_Conditions` `Human_Victory_Conditions`

**legacy docs/re/R1_T01_HARDPOINT_RUN.md**

`Collision_Mesh`

**legacy docs/re/REMAINING_GAPS.md**

`Chance_Affinity_Modifier` `Chance_Modifier_By_Base_Level` `Chance_Modifier_By_Planet_Difficulty` `Charge_Up_Seconds` `Cost_Affinity_Modifier` `Cost_Multiplier_By_Planet_Tech_Availability` `Damage_Multiplier` `Is_Stealth_Company` `Loads_When_Faction_Present` `Planet_Slice_Difficulty_Rating` `Planet_Starbase_Stealth` `Shield_Damage_Per_Second` `Space_Escort_Unit_Types`

**legacy docs/re/RENDER.md**

`Object_Fade_End`

**legacy docs/re/S78N_T14_RUN.md**

`Object_Max_Speed_Multiplier_Land`

**legacy docs/re/S9_DEATH_CLONE_SHIELDS.md**

`Damage_Types` `Projectile_Rocket_Straight_Distance`

**legacy docs/re/SCRIPT_GAPS.md**

`Projectile_Fire_Pulse_Delay_Seconds` `TSW_Attack_Anim_Duration_Seconds` `TSW_Power_Up_Countdown_Seconds` `TSW_SFXEvent_Activate_Voice`

**legacy docs/re/SKAI_T15_TARGET_LOCATION_FORCE.md**

`AI_LandAreaThreatScaleFactor`

**legacy docs/re/SKAI_T22_SPACE_AI_RE.md**

`AIUsesFogOfWarSpace`

**legacy docs/re/SKIRMISH_SETUP.md**

`Available_In_Skirmish` `Build_Limit_Current_For_All_Allies` `Build_Limit_Current_Per_Player` `Build_Limit_Lifetime_Per_Player` `Build_Tab_Land_Units` `Build_Tab_Space_Units` `Control_Point_Domination_Victory_Time_In_Secs` `Is_Skirmish_Tactical_Super_Weapon`

**legacy docs/re/SPACE_AND_OPEN.md**

`Damage_Decal` `Fire_Bone_A` `Fire_Bone_B` `In_Midforeground` `In_Midground` `No_Colorization_Color`

**legacy docs/re/STATION_HANGARS_SOUND.md**

`Fighter_Bay_Flyout_Distance` `SFXEvent_Ambient_Loop` `SFXEvent_Engine_Cinematic_Focus_Loop` `SFXEvent_Engine_Idle_Loop` `SFXEvent_Starbase_Ally_Upgraded` `SFXEvent_Starbase_Enemy_Upgraded` `Spawn_Garrison_On_Load` `Spawned_Squadron_Delay_Seconds` `Spawned_Squadron_Location_Bones` `Spawned_Squadron_Location_Flyout_Distances` `Spawned_Squadron_Types`

**legacy docs/re/STORY.md**

`Empire_Story_Name` `Rebel_Story_Name` `Reward_Type` `Story_Chapter` `Story_Incoming` `Story_Name` `Story_Tag` `Underworld_Story_Name`

**legacy docs/re/STORYB_T17_SPAWNS_AND_DEATH.md**

`Spin_Away_On_Death` `Spin_Away_On_Death_Explosion`

**legacy docs/re/STORY_PRESENTATION_LUA.md**

`Camera_FX_Manager_Letterbox_Height` `Color_End` `Color_Start` `Detail` `Displace` `Number_Bolts`

**legacy docs/re/STORY_REWARDS_GALACTIC.md**

`Base_Shield_Delay_Time` `Is_Debug_Switchable_To` `Object_Max_Health_Multiplier_Land` `Reward_Param_List` `Spawn_Indigenous_Units_Chance`

**legacy docs/re/STORY_T04_KESSEL_RUN.md**

`Timeout`

**legacy docs/re/STRATEGIC_GAPS.md**

`Always_Spawn_In_Orbit` `Cannot_Skip` `Commandbar_Offset` `Galactic_Zoom_In_Light_Angle` `Movie_File`

**legacy docs/re/STY1_GENERIC_AUTOSAVE.md**

`Drag_Select`

**legacy docs/re/TACTICAL_COMBAT.md**

`Allies` `Allow_Idle_When_Moving` `Apply_Special_Ability_To_Self_On_Creation` `Attack_Flinch_Interval_Seconds` `Attack_Priorities` `Base_Shield_Vulnerability_Modifier` `Blast_Ability_Fire_Projectile_Type` `Bribe_Modifier_On_Corrupted_Planets` `Can_Fighter_Fire_When_Idle` `Defense_Bonus_Percentage` `Enemies` `Enhancement_Percentage` `Fire_Cone_Height` `Fire_Cone_Width` `Fire_Inaccuracy_Distance` `Fire_Pulse_Delay_Seconds` `Fire_When_In_Defend_Mode` `Fire_When_In_Normal_Attack_Mode` `Fire_When_In_Rocket_Attack_Mode` `Fire_When_In_Turbo_Mode` `Hard_Point_Exclusions` `Hard_Point_Priorities` `Hull_Vs_Hard_Points_Health_Constraint` `Idle_Flinch_Interval_Seconds` `Induced_Flinch_Intensity` `Induced_Flinch_Range` `Is_Turret` `Lobbing_Superweapon_Chargeup_Particle` `Lobbing_Superweapon_SFXEvent_Chargeup` `Lobbing_Superweapon_SFXEvent_Fire` `Locomotor_Has_Animation_Priority` `Max_Secs_For_AE_Delayed_Damage` `Must_Face_Target_To_Attack_When_Deployed` `Presence_Induced_Animations` `Presence_Range` `Projectile_Blast_Area_Damage` `Projectile_Blast_Area_Max_Victims` `Projectile_Blast_Area_Range` `Projectile_Convert_Enemy_Radius` `Projectile_Damage_Delay_Secs` `Projectile_Damages_Random_Hard_Points` `Randomize_Between_Fire_Bones` `Redirect_Damage_To_Teammates` `Requires_Manual_Target_Assignment` `Share_Damage_With_Teammates` `ShieldRechargeIntervalInSecs` `Should_Attacker_Hold_Fire_For_Special_Ability` `Space_Retreat_Units_Damaged_Mod_Factor` `Specific_Faction` `Turret_Rotation_Offset` `Under_Construction_Damage_Multiplier` `Unit_Exclusions` `Unit_Presence_Relaxation_Time`

**legacy docs/re/TACTICAL_GAPS.md**

`Affects_All_Allies` `Confuse_Time_In_Secs` `Death_By_TSW_Replacements` `Detonation_Time_In_Secs` `Health_Per_Victim` `Land_Retreat_Attrition_Factor` `Preferred_Pathfinder_Types` `Projectile_Blast_Area_Immune_Faction` `Projectile_Combat_Mod` `Raid_Force_Max_Heros` `Raid_Force_Max_Limited_Objects` `Raid_Force_Required_Faction` `Space_Retreat_Attrition_Factor` `Spin_Away_On_Death_Chance` `TSW_Explosion_Debris_Creation_Frame_Delay` `TSW_Post_Destruction_Wait_Frames` `Tactical_Respawn_Time_In_Secs`

**legacy docs/re/TERRAIN_WATER_POST.md**

`Water_Clip_Plane_Offset` `Water_Render_Target_Resolution`

**legacy docs/re/TRN1_TERRAIN_GAMEPLAY.md**

`Base_Shield_Speed_Modifier` `Land_Retreat_Pursue_Max_Speed_Mod_Factor` `Projectile_Ion_Stun_Radius` `Projectile_Stun_Duration_Frames` `Space_Retreat_Pursue_Max_Speed_Mod_Factor`

**legacy docs/re/TRU1_A_RE_ANSWERS.md**

`Base_Shield_Always_Off` `Projectile_Ground_Detonation_Particle` `Projectile_Lifetime_Detonation_Particle` `Projectile_Weaken_Enemy_On_Detonation` `Shield_Normal_Color` `Shield_Upgraded_Color`

**legacy docs/re/TRU1_C_RE.md**

`Debris_Attached_Particle` `Debris_Facing_Rotate_Vector` `Debris_Max_Lifetime_Seconds` `Debris_Min_Lifetime_Seconds` `Debris_Movement_Vector` `Spawn_Indigenous_Units_In_Packs` `Spawn_Indigenous_Units_Radius` `Spawned_Indigenous_Pack_Type` `Spawned_Indigenous_Units_Delay_Seconds` `Spawned_Indigenous_Units_Quantity` `Spawned_Indigenous_Units_Type`

**legacy docs/re/TRU1_GRAVITY_FIELD.md**

`Gravity_Control_Field_Effect` `Gravity_Control_Field_Range` `Gravity_Control_Field_Slow_Fraction` `Is_Affected_By_Gravity_Control_Field`

**legacy docs/re/TSW_PLANET_DESTROY.md**

`Explosion_Jitter_Factor`

**legacy docs/re/UI.md**

`Anim_FPS` `Anim_Frame_Delay` `Animate_Back` `Animate_Upper_Effect` `Bar_Overlay_Name` `Bar_Texture_Name` `Base_Layer` `Base_Texture` `Blink_Fade` `Blink_Rate` `Build_Dial2_Offset` `Build_Dial_Offset` `Can_Animate` `Click_Shift` `Cursor_Texture_Name` `Default_Offset_Widescreen` `Disable_Darken` `Disabled_Offset` `Drag_And_Drop` `Drag_Back` `GUI_Bounds_Scale` `GUI_Bracket_Size` `GUI_Hide_Health_Bar` `Group` `Health_Bar_Spacing` `Icon_Offset` `Icon_Texture_Name` `Left_Justified` `Loop_Anim` `Lower_Effect_Additive` `Manual_Offset` `Max_Bar_Level` `Max_Text_Width` `Model_Name` `Mouse_Over_Offset` `No_Hidden_Collision` `No_Shell` `Outlined_Bar` `Overlay2_Offset` `Overlay_Offset` `Pathfinder_Slot_Index` `Pixel_Align` `Radar_Colorize_Selected_Units` `Reward_Param6` `Right_Justified` `Select_Box_Scale` `Select_Box_Z_Adjust` `Selected_Alpha` `Selection_Blob_Material_Name` `Smooth_Bar` `Tab` `Text_Color` `Text_Emboss` `Text_Offset` `Text_Outline` `Toggle` `Tutorial_Scene`

**legacy docs/re/WEATHER_VISUALS.md**

`Blob_Shadow_Below_Detail_Level` `Blob_Shadow_Bone_Name` `Blob_Shadow_Intensity` `Blob_Shadow_Scale`

**xml_tags.md**

`AI_LandThreatLookAheadTime` `AI_SpaceThreatLookAheadTime` `AI_Victory_Cycle_Limit` `Ability_Recharge_Bonus_Percentage` `Action_Distance_From_Target_Curve` `Action_Health_Percentage_Curve` `Activation_Chance` `Advisor_Hint_Interval` `Alternate_Icon_Name` `Always_Instantiate_Galactic` `Ambient_SFXEvent_Intermittent` `Apply_Damage_Right_To_Left` `Arc_Max_Range` `Arc_Min_Range` `Arc_Width_In_Degrees` `Armor_Types` `Audio_Galactic_3D_Listener_Z_Pullback_Dist` `Audio_Galactic_3D_Rolloff_Distance_Mod` `Audio_Galactic_3D_Saturation_Distance_Mod` `Audio_Land_3D_Listener_Z_Pullback_Dist` `Audio_Land_3D_Rolloff_Distance_Mod` `Audio_Land_3D_Saturation_Distance_Mod` `Audio_Space_3D_Listener_Z_Pullback_Dist` `Audio_Space_3D_Rolloff_Distance_Mod` `Audio_Space_3D_Saturation_Distance_Mod` `Basic_AI` `BeaconPlaceDelay` `Beam_Effect_Name` `Berserker_Damage` `Berserker_Damage_Range` `Berserker_Speed` `Big_Fleet_Color` `Bink_Player_Caption_Font_Size` `Bloom` `Bolt_Creation_Interval_Max` `Bolt_Creation_Interval_Min` `Bombardment_Initial_Delay` `Bombardment_Number_Of_Salvos` `Bombardment_Salvo_Delay` `Bombardment_Shot_Delay` `Bombardment_Shots_Per_Salvo` `Bottom_Bounds_Buffer` `Branch` `Build_Tab_Heroes` `Build_Tab_Outpost` `Build_Tab_Space_Station` `Build_Tab_Special_Structures` `CB_Movie_Offset` `Can_Be_Neutralized_By_Major_Heroes` `Can_Be_Neutralized_By_Minor_Heroes` `Can_Neutralize_Major_Heroes` `Can_Neutralize_Minor_Heroes` `Caption_Frame` `Collidable_By_Projectile_Dead` `Color_Zoomed_Out` `Confuse_Range` `Confuse_Travel_Time_In_Secs` `Contamination_Object_Name` `Control_Point_Domination_Condition_Relevant` `Corruption_1_Credit_Cost` `Corruption_1_Required_Mission_Count` `Corruption_1_Required_Mission_Type` `Corruption_1_Required_Unit_Count` `Corruption_1_Required_Unit_Type` `Corruption_1_Success_Bink_Hologram_Name` `Corruption_1_Success_Unlock_Unit_Type` `Corruption_1_Tactical_Game_Mode` `Corruption_1_Tactical_Map_Name` `Corruption_1_Tactical_Script_Name` `Corruption_2_Credit_Cost` `Corruption_2_Required_Mission_Count` `Corruption_2_Required_Mission_Type` `Corruption_2_Required_Unit_Count` `Corruption_2_Required_Unit_Type` `Corruption_2_Success_Bink_Hologram_Name` `Corruption_2_Success_Unlock_Unit_Type` `Corruption_2_Type` `Corruption_3_Credit_Cost` `Corruption_3_Required_Mission_Count` `Corruption_3_Required_Mission_Type` `Corruption_3_Required_Unit_Count` `Corruption_3_Required_Unit_Type` `Corruption_3_Success_Bink_Hologram_Name` `Corruption_3_Type` `Cost_Mod_By_Base_Level` `Cost_Mod_By_Planetary_Corruption_Level` `Cost_Mod_By_Previous_Neutralizations` `Credits_Bottom_Color` `Credits_Font` `Credits_Font_Size` `Credits_Header_Bottom_Color` `Credits_Header_Top_Color` `Credits_Margin` `Credits_Scroll_Rate` `Credits_Spacing` `Credits_Top_Color` `Crouch_Animation_Speed` `CurrentPathCostCoefficientSpace` `Cycles_Per_Distance` `Damage_Bonus_Percentage` `Damage_Hit_Particles` `Damage_Interval_In_Secs` `Damage_Range` `Damage_Sweep_Time_In_Secs` `Damage_To_Armor_Mod` `Death_SFXEvent_End_Die` `Default_Bounty_By_Category_MP` `Default_MSS_DIG_DS_FRAGMENT_CNT` `Default_MSS_DIG_DS_MIX_FRAGMENT_CNT` `Defend_Mode_Weapon_Delay_Multiplier` `Delay_Between_Space_Base_Attack_Announcement_Seconds` `Description_Text` `DestinationSearchRadiusIncrementSpace` `Disabled_Texture_Name` `Display_Font_Color` `Distance_Default` `Distance_Max` `Distance_Min` `Distance_Spline` `Drop_Phase_Duration` `DynamicLandQuotaResetInterval` `Encyclopedia_Class_Y_Offset` `Encyclopedia_Cost_Offset` `Encyclopedia_Good_Against` `Encyclopedia_Icon_X_Offset` `Encyclopedia_Icon_Y_Offset` `Encyclopedia_Name_Offset` `Encyclopedia_Unit_Class` `Encyclopedia_Vulnerable_To` `Energy_Pool_Bonus_Percentage` `Event_Duration` `Event_Model_Name` `Event_Model_Scale` `Event_Param2` `Event_Param3` `Event_Param4` `Event_Param5` `Event_Param6` `Event_Param7` `Event_Persistent` `Event_Single_Instance` `Event_Type` `Faction_Leader` `Faction_Leader_Company` `Fadein_End_Frame` `Fadein_Start_Frame` `Fadeout_Start_Frame` `Fadeout_Time_Max` `Fadeout_Time_Min` `Far_Clip` `FinalFacing180Penalty` `Fire_Min_Recharge_Seconds` `Fire_Range_Bonus_Percentage` `First_Hero_Conversation_Anim_Type` `First_Hero_Draw_Anim_Type` `First_Hero_Lose_Anim_Type` `First_Hero_Win_Anim_Type` `Flash_Texture_Name` `Font_Character_Padding` `Font_Size` `Font_Stretch_Factor` `Force_Cloak_Color` `Fov_Default` `Fov_Max` `Fov_Min` `FramesPerCollisionCheck` `GUI_Move_Acknowledge_Scale_Space` `Galactic_Advisor_Hints` `General_Major_Hero_Cost_Mod` `General_Minor_Hero_Cost_Mod` `Goal` `Grenade_Spawn_Frame` `Grenade_Toss_Anim` `HQ_Win_Condition_Relevant` `Halt_Game_Music` `HardPoint_Target_Reticle_Enemy_Texture` `HardPoint_Target_Reticle_Enemy_Tracked_Texture` `HardPoint_Target_Reticle_Friendly_Disabled_Texture` `HardPoint_Target_Reticle_Friendly_Disabled_Tracked_Texture` `HardPoint_Target_Reticle_Friendly_Repairing_Texture` `HardPoint_Target_Reticle_Friendly_Screen_Size` `HardPoint_Target_Reticle_Friendly_Texture` `HardPoint_Target_Reticle_Friendly_Tracked_Texture` `Has_Credits` `Has_Header` `Header_Font_Name` `Header_Font_Size` `Header_Text_IDs` `Header_Texture_Height_Pow_2` `Header_Texture_Width_Pow_2` `Heal_Amount` `Heal_Percent` `Health_Bonus_Percentage` `Health_Multiplier` `Hint_Text_Color` `Home_Planet` `Hot_X` `Hot_Y` `Human_Victory_Planet_Names`
