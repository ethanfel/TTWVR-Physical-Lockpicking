# SPDX-License-Identifier: GPL-3.0-only
"""Emit xNVSE/JIP scripts, scoped to the newly generated gallery cells."""


def write_scripts(output, m):
    root = output / "NVSE/user_defined_functions/TTWVRLockStage"
    root.mkdir(parents=True, exist_ok=True)

    def form(id):
        return f'GetFormFromMod "{m["plugin"]}" "{id[-6:]}"'

    def glob(key):
        return form(m["globals"][key])

    def write(name, text):
        (root / (name + ".txt")).write_text(text.strip() + "\n")

    common = f"""ref cells
ref currentCell
int page
let cells := {form(m["cells_list"])}
let currentCell := player.GetParentCell
let page := ListGetFormIndex cells currentCell"""
    write(
        "Enter",
        f"""ref marker
ref cells
ref currentCell
begin Function {{}}
 let cells := {form(m["cells_list"])}
 let currentCell := player.GetParentCell
 if (ListGetFormIndex cells currentCell) < 0
  let marker := {form(m["return_marker"])}
  marker.MoveTo player
  CallAfterFrames 3 (CompileScript "TTWVRLockStage/Arrive.txt") 0 currentCell
 else
  Console "coc TTWVRLockLab01"
 endif
end""",
    )
    write(
        "Arrive",
        f"""ref origin
ref marker
begin Function {{origin}}
 let marker := {form(m["return_marker"])}
 if (marker.GetParentCell) == origin
  Console "coc TTWVRLockLab01"
 else
  MessageBoxEx "The return location could not be recorded. Please try entering again."
 endif
end""",
    )
    write(
        "Control",
        f"""ref activated
ref actor
ref base
begin Function {{activated actor}}
 if actor != player
  return
 endif
 let base := {form(m["control_base"])}
 if (activated.GetBaseObject) == base
  Call (CompileScript "TTWVRLockStage/Menu.txt") 1
 endif
end""",
    )
    write(
        "Menu",
        f"""int section
begin Function {{section}}
 {common}
 ref menuGlobal
 ref bonusGlobal
 ref perk
 float bonus
 int rank
 string_var statusText
 if page < 0
  return
 endif
 let menuGlobal := {glob("menu")}
 if section == 1
  SetGlobalVariable menuGlobal 100
  MessageBoxExAlt (CompileScript "TTWVRLockStage/Button.txt") "GALLERY %.0f / {len(m["galleries"])}%rLockpick skill: %.0f|Reset locks|Refill pins to 200|Set skill|Perks|Next gallery|Previous gallery|First gallery|Return to entry|Close" (page+1) (player.GetAV Lockpick)
 elseif section == 2
  SetGlobalVariable menuGlobal 200
  MessageBoxExAlt (CompileScript "TTWVRLockStage/Button.txt") "LOCKPICK SKILL%rCurrent: %.0f. Lower skills retain the normal difficulty gates. Original removes the training modifier.|0|25|50|75|100|Original skill|Back" (player.GetAV Lockpick)
 else
  let perk := GetFormFromMod "FalloutNV.esm" "044CB0"
  let rank := player.HasPerkRank perk
  let bonusGlobal := {glob("bonus")}
  let bonus := GetGlobalVariable bonusGlobal
  let statusText := "Original"
  if bonus == 0
   let statusText := "Off"
  elseif bonus == 1
   let statusText := "On"
  endif
  SetGlobalVariable menuGlobal 300
  MessageBoxExAlt (CompileScript "TTWVRLockStage/Button.txt") "LOCKPICK PERKS%rInfiltrator: %.0f (0 off / 1 on)%rBobblehead effect: %z|Toggle Infiltrator|Bobblehead ON|Bobblehead OFF|Restore originals|Back" rank statusText
 endif
end""",
    )
    write(
        "Button",
        f"""int button
ref menuGlobal
float state
begin Function {{button}}
 let menuGlobal := {glob("menu")}
 let state := GetGlobalVariable menuGlobal
 if state >= 100 && button >= 0
  SetGlobalVariable menuGlobal ((state/10)+button)
 endif
end""",
    )
    branches = []
    for n, g in enumerate(m["galleries"]):
        branches.append(
            ("if" if n == 0 else "elseif")
            + f" page == {n}\n let lockList := {form(g['lock_list'])}"
        )
    write(
        "Reset",
        f"""begin Function {{}}
 {common}
 ref lockList
 ref target
 int n
 int level
 if page < 0
  return
 endif
 {chr(10).join(branches)}
 endif
 let n := 0
 while n < (ListGetCount lockList)
  let target := ListGetNthForm lockList n
  let level := (n % 5) * 25
  target.SetOpenState 0
  target.Lock level
  let n += 1
 loop
 PrintC "Lock stage: reset gallery %.0f, %.0f stations" (page+1) n
end""",
    )
    navigation = []
    for n, g in enumerate(m["galleries"]):
        navigation.append(
            ("if" if n == 0 else "elseif")
            + f' destination == {n}\n Console "coc {g["cell"]}"'
        )
    write(
        "Tick",
        f"""begin Function {{}}
 {common}
 ref deltaGlobal
 ref menuGlobal
 ref activeGlobal
 ref skillGlobal
 ref perkGlobal
 ref bonusGlobal
 ref originalBonus
 ref pins
 ref perk
 ref marker
 ref returnCell
 float delta
 float change
 float wanted
 float state
 int active
 int button
 int destination
 int amount
 int rank
 let deltaGlobal := {glob("skill_delta")}
 let menuGlobal := {glob("menu")}
 let activeGlobal := {glob("active")}
 let skillGlobal := {glob("skill")}
 let perkGlobal := {glob("original_infiltrator")}
 let bonusGlobal := {glob("bonus")}
 let originalBonus := {glob("original_bonus")}
 let perk := GetFormFromMod "FalloutNV.esm" "044CB0"
 let delta := GetGlobalVariable deltaGlobal
 let active := GetGlobalVariable activeGlobal
 if page < 0
  if delta != 0
   player.ModAV Lockpick (-delta)
   SetGlobalVariable deltaGlobal 0
  endif
  if active
   let rank := GetGlobalVariable perkGlobal
   if rank > 0
   player.SetPerkRank perk rank
  else
   player.RemovePerk perk
  endif
   SetGlobalVariable activeGlobal 0
  endif
  SetGlobalVariable menuGlobal 0
  if currentCell && (MenuMode == 0) && (GetINIFloat "Stage:Enter" "TTWVRLockStage.ini") == 1
   SetINIFloat "Stage:Enter" 0 "TTWVRLockStage.ini"
   Call (CompileScript "TTWVRLockStage/Enter.txt")
  endif
  return
 endif
 if active == 0
  SetGlobalVariable perkGlobal (player.HasPerkRank perk)
  SetGlobalVariable originalBonus (GetNumericGameSetting "fLockpickBonusHealth")
  SetGlobalVariable bonusGlobal 2
  SetGlobalVariable activeGlobal 1
 endif
 let wanted := GetGlobalVariable skillGlobal
 if wanted < 0
  let change := -delta
 else
  let change := wanted - player.GetAV Lockpick
 endif
 if change != 0
  player.ModAV Lockpick change
  SetGlobalVariable deltaGlobal (delta+change)
 endif
 let state := GetGlobalVariable menuGlobal
 if state >= 100
  return
 endif
 if state < 10 || MenuMode
  return
 endif
 SetGlobalVariable menuGlobal 0
 if state == 10
  Call (CompileScript "TTWVRLockStage/Reset.txt")
 elseif state == 11
  let pins := GetFormFromMod "FalloutNV.esm" "00000A"
  let amount := 200 - player.GetItemCount pins
  if amount > 0
   player.AddItem pins amount 1
  endif
 elseif state == 12
  Call (CompileScript "TTWVRLockStage/Menu.txt") 2
 elseif state == 13
  Call (CompileScript "TTWVRLockStage/Menu.txt") 3
 elseif state >= 14 && state <= 16
  let destination := 0
  if state == 14
   let destination := (page+1) % {len(m["galleries"])}
  elseif state == 15
   let destination := (page+{len(m["galleries"])}-1) % {len(m["galleries"])}
  endif
  {chr(10).join(navigation)}
  endif
 elseif state == 17
  let marker := {form(m["return_marker"])}
  let returnCell := marker.GetParentCell
  if (ListGetFormIndex cells returnCell) < 0
   player.MoveTo marker
  else
   MessageBoxEx "No entry location was recorded. Load your original save to leave."
  endif
 elseif state >= 20 && state <= 24
  SetGlobalVariable skillGlobal ((state-20)*25)
 elseif state == 25
  SetGlobalVariable skillGlobal -1
 elseif state == 26 || state == 34
  Call (CompileScript "TTWVRLockStage/Menu.txt") 1
 elseif state == 30
  let rank := 1 - player.HasPerkRank perk
  if rank > 0
   player.SetPerkRank perk rank
  else
   player.RemovePerk perk
  endif
 elseif state == 31
  SetGlobalVariable bonusGlobal 1
 elseif state == 32
  SetGlobalVariable bonusGlobal 0
 elseif state == 33
  let rank := GetGlobalVariable perkGlobal
  if rank > 0
   player.SetPerkRank perk rank
  else
   player.RemovePerk perk
  endif
  SetGlobalVariable bonusGlobal 2
 endif
end""",
    )
    # Mod-local data survives save loads, but disappears on process exit.
    # A normal save has no stage globals, so globals alone cannot undo GMST edits.
    effect = (
        """if mode == 0
   let desired := 0.01
  elseif mode == 1
   let desired := 0.015
  endif"""
        if m["bobblehead_effect_verified"]
        else ""
    )
    write(
        "BonusTick",
        f"""begin Function {{}}
  {common}
  ref activeGlobal
  ref originalGlobal
  ref bonusGlobal
  float desired
  float mode
  let activeGlobal := {glob("active")}
  if page >= 0 && (GetGlobalVariable activeGlobal) == 1
   let originalGlobal := {glob("original_bonus")}
   let bonusGlobal := {glob("bonus")}
   if eval (ModLocalDataExists "TTWVRLockStage.BonusBaseline") == 0
    SetModLocalData "TTWVRLockStage.BonusBaseline" (GetGlobalVariable originalGlobal)
   endif
   let desired := GetModLocalData "TTWVRLockStage.BonusBaseline"
   let mode := GetGlobalVariable bonusGlobal
   {effect}
   SetNumericGameSetting "fLockpickBonusHealth" desired
  elseif eval page < 0 && (ModLocalDataExists "TTWVRLockStage.BonusBaseline")
   let desired := GetModLocalData "TTWVRLockStage.BonusBaseline"
   SetNumericGameSetting "fLockpickBonusHealth" desired
   RemoveModLocalData "TTWVRLockStage.BonusBaseline"
  endif
end""",
    )
    write(
        "Install",
        """begin Function {}
 SetEventHandler "OnActivate" (CompileScript "TTWVRLockStage/Control.txt")
 SetGameMainLoopCallback (CompileScript "TTWVRLockStage/Tick.txt") 1 5 3
 SetGameMainLoopCallback (CompileScript "TTWVRLockStage/BonusTick.txt") 1 5 3
end""",
    )
    scripts = output / "NVSE/Plugins/scripts"
    scripts.mkdir(parents=True, exist_ok=True)
    (scripts / "gr_TTWVRLockStage.txt").write_text(
        'Call (CompileScript "TTWVRLockStage/Install.txt")\n'
    )
    config = output / "config"
    config.mkdir(exist_ok=True)
    (config / "TTWVRLockStage.ini").write_text("[Stage]\nEnter=0\n")
