# Galactic sell and the paid cost (#191)

Retail FoC x64 `StarWarsG.exe` (v1.121.859328), read in the Ghidra export and the disassembly (reference only, not
copied) on 2026-11-25. Tiers: **[H]** read directly in the code, **[M]** inferred from the code with a gap named, **[L]**
a guess. Follows legacy docs/re/COMMANDS.md §2.3 (command 46) and GALACTIC_ECONOMY.md §2.5 (the paid cost +0x32c).

## The executor: GalacticSellEventClass::Execute 0x5327a0 [H]

GalacticSellEventClass (vtable 0x890f70): slot 4 0x5327a0 executes, slot 5 0x532740 packs the id (not packed when it
is 0x3FFFFF).

1. The event's object id (+0x48) is 0x3FFFFF, or there is no galactic object manager: nothing happens.
2. The object is found by id in the galactic object manager (0x2aca60). Not found: nothing happens. There is **no
   owner check** and no test of the object's kind.
3. An object with a TRANSPORT slot (+0x337 != -1) first removes every object it carries (the list 0x397bf0, +0xC8).
4. The objects attached to it (the list at +0xC0, collected recursively by 0x3965f0) are removed.
5. The refund is read: `paid` = the object's +0x32c (float), and `half` = Build_Cost_Credits (0x3711c0 with no player:
   the type's +0x86c) of the type 0x3973b0 derives from the object, halved as an int (`cdq; sub; sar`, toward zero).
6. The object is removed. Every removal in this executor is 0x391310(object, 1), not the death handler.
7. The amount:
   - `paid > 1.0`: round(paid / 2.0), 0x22f9c0 rounding half away from zero in float;
   - `0.0 < paid <= 1.0`: nothing;
   - `paid <= 0.0`: `half`.
8. When the amount is above 0 and the event's player (+0x24, the issuer) exists, 0x27f370(player, amount, 0) adds
   the credits with no notification. The owner is never asked: the issuer is paid.

## The paid cost +0x32c [H]

- The production service 0x402840 writes it when the queue head completes and an object was created. A created fleet
  (+0x336 != -1) passes it to its first member (0x3965f0); any other object takes it itself.
- The value is 0x499000(entry): the entry's cost (+0x28) for an up-front payer; for a pay-as-you-go payer (0x289050)
  cost x clamp(1 - remaining (+0x1c) / build time (+0x10), 0, 1), the comparisons written so that a NaN reads as 1.
  At completion the remaining time is 0, so both give the full cost.
- The other writers are constructors (0) and object copies (the source's value). An object that was never produced
  (starting forces, story spawns, reinforcements) keeps 0, and its sale refunds half its build cost.

## Port (#191)

- `GalacticSim::paid_cost` / `set_paid_cost`: an ascending (ObjectId, credits) list holding the non-zero values,
  written by `complete_build` with 0x499000's formula, dropped when the object is destroyed, hashed ("PDC1", once
  non-empty) and saved (galactic save v16; older saves read none).
- `GalacticSim::sell` and the `GalacticSell` case of `GalacticSim::execute`: the rule above. `destroy_object` removes
  a fleet's members with it.
- [port] The type is the object's own, not 0x3973b0's derivation from behaviours 4 / 5 / 0x16 [M]. This is tier 1 for
  the shipped structures, which derive their own type.
- [port] The score's read of +0x32c is not modelled (tier 3: nothing reads it yet).
