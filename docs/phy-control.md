# LAN8742 startup control

This milestone adds CMSIS MDIO writes and a nonblocking PHY reset/auto-negotiation
controller. It does not implement MAC/DMA frames, lwIP, IP or ping. M7 remains the
single Ethernet owner; M4 remains the unchanged assembly parking image.

## Transport and ownership

MdioTransport replaces the earlier MdioReader name. begin/poll retain the read API;
begin_write/poll_write add explicit write completion. One owner serializes both
operations. Unconsumed completion still owns the bus even after hardware MB clears.
Wrong-operation polling cannot consume a result. Write data is staged before the
command/busy bit; neither address nor data is overwritten while busy or faulted.
A write completion means the MAC transaction finished, not that the PHY accepted
or retained the value; startup explicitly verifies the relevant control state.
Timeout latches the transport and does not cancel hardware. Only the existing
explicit recover_after_idle operation can release that fault after hardware idle.

Lan8742Control borrows that transport and a validated PHY address. Both outlive its
pending work. The application constructs the controller and link monitor in fixed
optional storage and lets only one use MDIO at a time. No heap, global service
locator, virtual hierarchy or new thread is introduced. This is an explicit enum
state machine with injected ports, following the existing embedded safety profile.

## Sequence and deadlines

1. Write BMCR 0x8000 alone. Poll BMCR until reset clears; all-ones responses never
   count as a completed reset. No AN write occurs before this observation.
2. Write BMCR 0x1200 (AN enable plus restart). Preserve the reset-default advertisement;
   this milestone does not configure ANAR or force speed/duplex.
3. Read BMCR: reject reset/loopback/power-down/isolation or disabled AN; wait for the
   restart bit to clear. Only then is configuration treated as verified.
4. Double-read BMSR to clear latched state. Require current link plus AN completion,
   decode PHY special status, and verify live BMSR again before publishing ready.

Default timing is 20ms per MDIO operation, 600ms for the whole reset phase, 5000ms
for configuration/negotiation and a 10ms polling interval. The datasheet specifies
software reset completion within 500ms; 600ms is this project's observation margin.
The 5s negotiation budget is project policy, not a guarantee about every link partner.
Each MDIO request is capped by the remaining phase budget; no new request starts
at/after that deadline. A stuck transfer remains a transport fault requiring explicit
idle recovery. A phase result observed too late is rejected. Caller polling/timebase
must progress and must not miss a complete 32-bit tick cycle; this is not an
independent watchdog for a failed SysTick or stalled main loop.

poll does bounded work and never spins waiting for the PHY. Reset, unverified
configuration and ordinary link negotiation timeouts are distinct errors. A normal
negotiation timeout is returned only after the required BMCR configuration was
verified and with no outstanding MDIO result. It never reports stale link-up.

## Application behavior and diagnostics

PHY discovery is followed by asynchronous control in Application::poll. The green
heartbeat remains 3000ms per state and runs during PHY management. Once setup
succeeds, the normal 250ms read-only link snapshots resume.

A missing cable can exhaust initial negotiation. The application then keeps AN
enabled and passively observes link; it does not repeatedly reset or restart the PHY.
Later cable insertion can reach ready through the existing link monitor. Reset,
unverified-configuration or transport failures remain visible with red LED and no
further management requests; they do not stop heartbeat or publish link-up. The
firmware provides no automatic retry/recovery command for these hardware faults yet.

Existing g_bringup (40 bytes) and g_link (12 bytes) layouts are preserved. New g_phy
is seven uint32 words: state, error, attempts, completed, startup_bmcr, startup_bmsr, startup_mode. The last three capture startup readback from the firmware-owned bus; they are not a continuously refreshed register dump. Values of state are idle=0,
resetting=1, negotiating=2, ready=3, failed=4, waiting_link=5. Error zero means none;
other values follow PhyError in phy_control.hpp. A pending negotiation timeout is
retained while waiting for a cable and cleared after a real resolved link observation.
These debugger fields have one M7 writer; they are not an atomic multicore protocol.
Use symbols from the actual ELF rather than addresses from older images. Do not issue debugger MDIO commands while the application owns that bus; read the captured values and current link diagnostics instead.

## Verification and hardware acceptance

Observed behavioral red tests preceded MDIO write, PHY control and application
integration. Host tests use the real transport/controller with simulated registers.
They cover exact command/data ordering, shared ownership, wrong polling, reset and
restart stuck bits, all four link modes, invalid BMCR/status, missing cable, lost link
during mode sampling, late polling, MDIO faults and tick rollover. Application tests
also cover continued heartbeat and late cable recovery without extra reset writes.
These are software integration tests, not physical PHY or cable observations.

Before writing a candidate, require its green pre-flash CI plus local gates. Then,
within explicit hardware authorization, archive the image/hash and full Flash backup;
verify PHY identity/control readback and g_phy transitions on the exact ordinary
Release; observe real cable removal/reconnection and heartbeat; confirm no fault or
MDIO/monitor errors in the nominal case. Preserve M4 and option/boot bytes and keep
the previously verified Release available for rollback. Test standalone cold startup
without debugger initialization before release; software reset does not replace the
datasheet's required hardware reset after power-up. Required HIL remains pending
until actually performed; no hardware writes are implied by implementing this code.

Coverage of changed modules uses fresh host profiles plus fresh target compiler line
inventory with zero target hits. Historical hardware hits may be retained only for
source-SHA-identical files and must be identified separately; they do not establish
new PHY-control execution or exact Release acceptance.

Primary references:
- Microchip LAN8742A DS00001989A, sections 3.2.2, 3.8.6.2 and 4.2.1/4.2.2/4.2.22:
  https://www.microchip.com/content/dam/mchp/documents/OTH/ProductDocuments/DataSheets/DS_LAN8742_00001989A.pdf
- ST RM0399, Ethernet MACMDIOAR/MACMDIODR; command/data encodings are also checked
  against the pinned STM32H755 CMSIS headers by static_assert:
  https://www.st.com/resource/en/reference_manual/dm00176879-stm32h745-755-and-stm32h747-757-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf

Local Sonar acceptance also rejects parser errors even when scanner exit and the server gate succeed. The strict CXX mode and log guard preserve this check; initialized members are declared separately for the installed parser, with no source exclusions.
