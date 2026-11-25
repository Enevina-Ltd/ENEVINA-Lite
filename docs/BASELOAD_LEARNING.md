# Learned shared-meter OFF load

The controller learns a local OFF-load reference from seven independent
meter/adapter observations in a continuous OFF request, after at least four
minutes of OFF, spanning at least five minutes. The observations must be
fresh, associated with the same configured meter, same request/source
generation, and stable within a 15 W / 10% tolerance. The median is retained
as a reference; replay, a counter reset, a different meter or a material
change withdraws or renews qualification. The last reference is usable for
at most two hours. No extra polling task or new lock is introduced.

The reference is the entire measured OFF load, **including pump standby**,
not measured heat output. Unknown-scope meters can learn and display it,
but cannot enter the consumption fit. Select the existing whole-building
meter scope to enable estimated incremental electric energy. Only slots
with the same qualified reference at both boundaries, fresh monotonic
counter readings and a qualified pump request are used. The predicted
increment is labelled estimated, never an isolated heat-pump measurement.
Unrelated variable loads during heating remain a source of uncertainty;
the existing electrical validation thresholds still apply.

The thermal identifier already estimates the constant internal/solar heat
gain from measured indoor response. Do not add the electrical OFF watts
to this gain: an absolute thermal scale and heat-delivery fraction are not
known. An observed change in the qualified background regime (>25 W or
15%, whichever is larger) withdraws prior thermal validation and renews
the gain coefficient's uncertainty, retaining the building's observations.
The derived electric fit restarts. The MPC uses this room-derived gain
exactly once and, once validated, the incremental electric fit for
price-weighted decisions. An unavoidable constant background electricity
cost is independent of the pump decision and is not included in its
incremental cost. A shared-meter estimate never yields COP or absolute
UA/capacity bounds. The fallback price controller is unchanged.

Direct pump-only meter models keep their existing persistence layout.
Shared-meter models carry an explicit source tag, CRC and baseline regime
in an extended image. New code reads the old model formats. Earlier
firmware does not interpret the extended shared-meter model (it can
relearn rather than interpreting a derived estimate as a direct meter).
The baseline itself is requalified after reboot; stored electrical and
thermal parameters are not mistaken for new meter observations.

Test plan: robust OFF load, change/identity/clock/replay/counter resets,
mixed load rejection, direct/shared source separation, shared model
save/restore and gain revalidation, simulated MPC forecast, FI/EN UI,
host/ESP32 builds. Physical commissioning must separately confirm the
actual meter scope and stable OFF observations in the installation.
