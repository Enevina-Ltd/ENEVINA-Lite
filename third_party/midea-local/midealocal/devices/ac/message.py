"""Midea AC message."""

import logging
from collections.abc import Callable, Mapping
from enum import IntEnum, StrEnum
from types import MappingProxyType

from midealocal.base_classes.climate import MideaFanMode, MideaSwingMode
from midealocal.const import MAX_BYTE_VALUE, DeviceType
from midealocal.crc8 import calculate
from midealocal.message import (
    BoolParser,
    FloatParser,
    FunctionParser,
    IntParser,
    ListTypes,
    MessageBody,
    MessageHeaderAA,
    MessageRequest,
    MessageResponse,
    MessageType,
    NewProtocolMessageBody,
)

_LOGGER = logging.getLogger(__name__)

A1_MIN_BODY_LENGTH = 18

AA_MESSAGE_TYPE_INDEX = 9
BB_AC_MODES = [0, 3, 1, 2, 4, 5]
BB_MIN_BODY_LENGTH = 21
BB_FRESH_AIR_SWITCH_INDEX = 45
BB_FRESH_AIR_INTAKE_STATUS_MASK = 0x01
BB_FRESH_AIR_EXHAUST_STATUS_MASK = 0x02
BB_FRESH_AIR_INTAKE_SPEED_INDEX = 46
BB_FRESH_AIR_EXHAUST_SPEED_INDEX = 47
BB_COMPRESSOR_TARGET_FREQUENCY_INDEX = 10
BB_COMPRESSOR_FREQUENCY_INDEX = 11
BB_BASIC_FAN_SPEED_INDEX = 7
BB_INDOOR_TEMPERATURE_HIGH_INDEX = 8
BB_INDOOR_HUMIDITY_INDEX = 30
BB_SN8_FLAG_INDEX = 80
BB_OUTDOOR_TEMPERATURE_HIGH_INDEX = 6
BX_FILTER_STATUS_LENGTH = 5
CONFORT_MODE_MIN_LENGTH = 16
CONFORT_MODE_MIN_LENGTH2 = 23
SMART_DRY_MIN_LENGTH = 20
SWING_LR_MIN_LENGTH = 21
FRESH_AIR_C0_MIN_LENGTH = 29
ECO_MODE_MIN_SUBPROTOCOL_LENGTH = 27
FRESH_AIR_LENGTH = 2
FROST_PROTECT_MIN_LENGTH = 22
INDIRECT_WIND_VALUE = 0x02
MAX_MSG_SERIAL_NUM = 254
OUT_SILENT_VALUE = 0x03
POWER_SAVING_VALUE = 0x08
SCREEN_DISPLAY_BYTE_CHECK = 0x07
SUB_PROTOCOL_BODY_TEMP_CHECK = 0x80
TEMP_DECIMAL_MIN_BODY_LENGTH = 20
TIMER_MIN_SUBPROTOCOL_LENGTH = 27
XBB_SN8_BYTE_FLAG = 0x31
XC1_SUBBODY_TYPE_03 = 0x03
XC1_SUBBODY_TYPE_40 = 0x40
XC1_SUBBODY_TYPE_41 = 0x41
XC1_SUBBODY_TYPE_42 = 0x42
XC1_SUBBODY_TYPE_43 = 0x43
XC1_SUBBODY_TYPE_44 = 0x44
XC1_SUBBODY_TYPE_45 = 0x45
XC1_SUBBODY_TYPE_47 = 0x47
XC1_SUBBODY_TYPE_INDEX = 3
XC1_HUMIDITY_INDEX = 4
XC1_CONSUMPTION_MIN_LENGTH = 19
XC1_OPERATING_TIME_MIN_LENGTH = 19

# Group data query: the third payload byte selects the group, 0x40 | group number.
XC1_GROUP_QUERY_BASE = 0x40
XC1_GROUP_THREE_SPEED_OFFSET = 10
# Minimum body length required to parse each group data response.
XC1_GROUP_ONE_MIN_LENGTH = 15
XC1_GROUP_TWO_MIN_LENGTH = 9
XC1_GROUP_SEVEN_MIN_LENGTH = 12
# Refrigerant circuit temperatures are reported as half degrees with an offset:
# T1/T2 (indoor ambient / indoor coil) use 30, T3/T4 (outdoor coil / ambient) use 50.
TEMP_INDOOR_OFFSET = 30
TEMP_OUTDOOR_OFFSET = 50
TEMP_DIVISOR = 2
# Indoor fan speed is reported in units of 8 RPM.
XC1_FAN_SPEED_FACTOR = 8
# Bit 4 of group 2 byte 8 indicates the condensate water pump is running.
XC1_WATER_PUMP_MASK = 0x10

BB_FRESH_AIR_CONTROL_BODY_LENGTH = 96
BB_FRESH_AIR_CONTROL_CURSOR = 15
BB_FRESH_AIR_INTAKE_SWITCH_MASK = 0x04
BB_FRESH_AIR_EXHAUST_SWITCH_MASK = 0x08
BB_FRESH_AIR_INTAKE_CONTROL_SPEED_INDEX = 55
BB_FRESH_AIR_EXHAUST_CONTROL_SPEED_INDEX = 56
BB_FRESH_AIR_SPEED_FLAG = 0x80

# Model 22013279 reports temperatures in this new-protocol tag because its
# standard C0 temperature fields are stale.
NEW_PROTOCOL_TEMPERATURE_TAG = 0x7E
NEW_PROTOCOL_TEMPERATURE_MIN_LENGTH = 41
NEW_PROTOCOL_SETPOINT_OFFSET = 11.5
NEW_PROTOCOL_SETPOINT_MASK = 0x3F
NEW_PROTOCOL_SETPOINT_HALF_DEGREE_BIT = 0x40
NEW_PROTOCOL_MIN_VALID_TEMPERATURE = 10
NEW_PROTOCOL_MAX_VALID_TEMPERATURE = 40
NEW_PROTOCOL_LEGACY_SETPOINT_BYTE = 3
NEW_PROTOCOL_INDOOR_TEMPERATURE_BYTE = 40
NEW_PROTOCOL_INDOOR_TEMPERATURE_DECIMAL_BYTE = 41

# B5 capability value semantics (reverse-engineered; see _parse_capabilities).
# The raw byte of each capability is not a 0/1 flag; each has its own value set.
B5_HEAT_MODE_VALUES = frozenset({1, 2, 4, 6, 7, 9, 10, 11, 12, 13})
B5_NO_COOL_MODE_VALUES = frozenset({2, 10, 12})
B5_DRY_MODE_VALUES = frozenset({0, 1, 5, 6, 9, 11, 13})
B5_AUTO_MODE_VALUES = frozenset({0, 1, 2, 7, 8, 9, 13})
B5_SWING_HORIZONTAL_VALUES = frozenset({1, 3})
B5_LOW_VALUE_MAX = 2  # value < 2 -> vertical swing / cool turbo available
B5_FAN_LOW_HIGH_VALUES = frozenset({3, 4, 5, 6, 7, 9})
B5_FAN_MEDIUM_VALUES = frozenset({5, 6, 7})
B5_FAN_AUTO_VALUES = frozenset({4, 5, 6, 9})
B5_FAN_SILENT_VALUES = frozenset({6, 9})
B5_FAN_CUSTOM_VALUE = 1
B5_ECO_VALUES = frozenset({1, 2})
B5_ANION_ON_VALUE = 1
B5_TURBO_HEAT_VALUES = frozenset({1, 3})
B5_DISPLAY_VALUES = frozenset({1, 2, 100})
B5_ELECTRICITY_UNSUPPORTED_VALUE = 0  # 0 = unsupported; nonzero = rate level count
B5_ENERGY_STATS_VALUES = frozenset({2, 3, 4, 5})
B5_ENERGY_SETTING_VALUES = frozenset({3, 5})
B5_ENERGY_BCD_VALUES = frozenset({2, 3})
RATE_SELECT_2_LEVEL_BIT = 0x1
RATE_SELECT_5_LEVEL_BIT = 0x2
B5_HUMIDITY_SUPPORTED_MASK = 0x3

# Indoor humidity is a percentage (1..100). 0x00 (not reported) and 0xFF
# (sensor absent or faulted) are placeholders, not real readings.
INVALID_INDOOR_HUMIDITY = frozenset({0, MAX_BYTE_VALUE})


def parse_indoor_humidity(raw: int) -> int | None:
    """Return the indoor humidity byte, or None for the invalid placeholders."""
    return None if raw in INVALID_INDOOR_HUMIDITY else raw


class DeviceAttributes(StrEnum):
    """Midea AC device attributes."""

    prompt_tone = "prompt_tone"
    power = "power"
    mode = "mode"
    target_temperature = "target_temperature"
    min_temperature = "min_temperature"
    max_temperature = "max_temperature"
    fan_speed = "fan_speed"
    swing_vertical = "swing_vertical"
    swing_horizontal = "swing_horizontal"
    boost_mode = "boost_mode"
    follow_me = "follow_me"
    power_saving = "power_saving"
    smart_eye = "smart_eye"
    dry = "dry"
    eco_mode = "eco_mode"
    aux_heating = "aux_heating"
    sleep_mode = "sleep_mode"
    natural_wind = "natural_wind"
    temp_fahrenheit = "temp_fahrenheit"
    screen_display = "screen_display"
    screen_display_alternate = "screen_display_alternate"
    full_dust = "full_dust"
    frost_protect = "frost_protect"
    comfort_mode = "comfort_mode"
    indoor_temperature = "indoor_temperature"
    outdoor_temperature = "outdoor_temperature"
    indirect_wind = "indirect_wind"
    indoor_humidity = "indoor_humidity"
    breezeless = "breezeless"
    fresh_air_power = "fresh_air_power"
    fresh_air_fan_speed = "fresh_air_fan_speed"
    fresh_air_mode = "fresh_air_mode"
    fresh_air_1 = "fresh_air_1"
    fresh_air_2 = "fresh_air_2"
    fresh_air_exhaust_power = "fresh_air_exhaust_power"
    fresh_air_exhaust_speed = "fresh_air_exhaust_speed"
    fresh_air_exhaust_mode = "fresh_air_exhaust_mode"
    total_energy_consumption = "total_energy_consumption"
    total_operating_consumption = "total_operating_consumption"
    current_energy_consumption = "current_energy_consumption"
    realtime_power = "realtime_power"
    electrify_time = "electrify_time"
    total_operating_time = "total_operating_time"
    current_operating_time = "current_operating_time"
    wind_lr_angle = "wind_lr_angle"
    wind_ud_angle = "wind_ud_angle"
    rate_select = "rate_select"
    out_silent = "out_silent"
    anion = "anion"
    sound = "sound"
    self_clean = "self_clean"
    pmv = "pmv"
    error_code = "error_code"
    # group 1: compressor and refrigerant circuit
    compressor_frequency = "compressor_frequency"
    target_compressor_frequency = "target_compressor_frequency"
    compressor_current = "compressor_current"
    compressor_voltage = "compressor_voltage"
    indoor_ambient_temperature = "indoor_ambient_temperature"  # T1
    indoor_coil_temperature = "indoor_coil_temperature"  # T2
    outdoor_coil_temperature = "outdoor_coil_temperature"  # T3
    outdoor_ambient_temperature = "outdoor_ambient_temperature"  # T4
    discharge_pipe_temperature = "discharge_pipe_temperature"  # TP
    # group 2: indoor fan and condensate pump
    indoor_fan_speed = "indoor_fan_speed"
    target_indoor_fan_speed = "target_indoor_fan_speed"
    water_pump_running = "water_pump_running"
    # group 3: outdoor fan
    outdoor_fan_speed = "outdoor_fan_speed"
    # group 7: real time compressor power
    compressor_power = "compressor_power"


class ACFanSpeed(MideaFanMode):
    """AC fan speed set-points.

    fan_speed is reported as a 0-127 percentage-like value; a device only
    ever commands one of these six set-points, but reported values are
    bucketed by threshold (against the next-lower set-point) to tolerate
    slightly-off readings from hardware.
    """

    SILENT = 20
    LOW = 40
    MEDIUM = 60
    HIGH = 80
    FULL = 100
    AUTO = 102


class ACSwingMode(MideaSwingMode):
    """AC swing mode names.

    A StrEnum (not IntEnum, unlike ACFanSpeed): its members compare equal to
    plain strings, which is required since Home Assistant's climate contract
    checks membership with bare strings (e.g. "off" in device.swing_modes).
    """

    OFF = "off"
    VERTICAL = "vertical"
    HORIZONTAL = "horizontal"
    BOTH = "both"


class PowerFormats(IntEnum):
    """AC Power/Energy analysis formats."""

    # unless stated, consumption / energy is 0.01 kWh, and power in 0.1 W resolution
    BCD = 1
    BINARY = 2  # binary with energy in 0.1 kWh resolution
    MIXED = 3  # mixed/INT (byte = 0-99)
    BINARY1 = 12  # binary
    BCD_ENERGY_BINARY_POWER = 101


# Power formats whose energy counters are BCD (B5 energy_bcd is true).
BCD_ENERGY_FORMATS = frozenset(
    {PowerFormats.BCD, PowerFormats.BCD_ENERGY_BINARY_POWER},
)


class NewProtocolTags(IntEnum):
    """New protocol tags in query and response."""

    arom = 0x0069
    auto_prevent_straight_wind = 0x0226
    b5_anion = 0x021E
    b5_eco = 0x0212
    b5_electricity = 0x0216
    b5_fahrenheit = 0x0222
    b5_filter_check = 0x0221
    b5_filter_remind = 0x0217
    b5_humidity = 0x021F
    b5_mode = 0x0214
    b5_ptc = 0x0219
    b5_screen_display = 0x0224
    b5_sound = 0x022C
    b5_strong_wind = 0x021A
    b5_temperature = 0x0225
    b5_wind_speed = 0x0210
    b5_wind_swing = 0x0215
    breezeless = 0x0018  # queryType == "fn_no_wind_sense"
    buzzer_all = 0x022C
    child_lock = 0x005C
    child_prevent_cold_wind = 0x003A
    cool_hot_sense = 0x0021
    degerming = 0x005A
    dust_full_time_reset = 0x00B8
    error_code_query = 0x003F
    even_wind = 0x004E
    extreme_wind = 0x004C
    face_register = 0x0044
    filter_level = 0x0409
    filter_status = 0x003D
    fresh_air_1 = 0x0233
    fresh_air_2 = 0x004B  # queryType == "fresh_air"
    fresh_air_parm = 0x0250
    fresh_filter_reset = 0x0085
    gentle_wind_sense = 0x0043
    high_temp_remove_odor_alone = 0x005E
    high_temperature_monitor = 0x0047
    indirect_wind = 0x0042  # prevent_straight_wind
    indoor_humidity = 0x0015  # queryType == "indoor_humidity"
    intelligent_control = 0x0031
    intelligent_wind = 0x0034
    light = 0x005B
    little_angel = 0x021B
    mode_query = 0x0041
    nobody_energy_save = 0x0030
    offline_operating_time = 0x022B
    operating_time = 0x0228
    out_silent = 0x00CD
    ozone = 0x005F
    parent_control = 0x0051
    pm25_value = 0x020B
    pre_cool_hot = 0x0201
    prevent_straight_wind_lr = 0x0058
    prevent_super_cool = 0x0049
    prompt_tone = 0x001A  # buzzerValue
    ptc_lock = 0x0229
    rate_select = 0x0048
    remote_control_lock = 0x0227  # power_lock?
    rewarming_dry = 0x0068
    screen_display = 0x0017
    security = 0x0029
    # Live self-clean status. Reported in B0 (set echo) and B1 (query) bodies.
    # The same tag in a B5 capability body only advertises support, not state.
    self_clean = 0x0039
    self_remove_odor_phase = 0x005D
    single_tuyere = 0x004F
    soft_warm = 0x0063
    voice_control = 0x0020
    volume_control = 0x0024
    water_pump = 0x0050
    water_washing = 0x004A
    wind_around = 0x0059
    wind_avoid = 0x0033
    wind_lr_angle = 0x000A
    wind_straight = 0x0032
    wind_top = 0x0061
    wind_ud_angle = 0x0009
    wet_film_reset = 0x00CC


class MessageACBase(MessageRequest):
    """AC message base."""

    _message_serial = 0
    # body type of the reply, None when it is not known
    _response_body_type: ListTypes | None = None

    def __init__(
        self,
        protocol_version: int,
        message_type: MessageType,
        body_type: ListTypes,
    ) -> None:
        """Initialize AC message base."""
        super().__init__(
            device_type=DeviceType.AC,
            protocol_version=protocol_version,
            message_type=message_type,
            body_type=body_type,
        )
        MessageACBase._message_serial += 1
        if MessageACBase._message_serial >= MAX_MSG_SERIAL_NUM:
            MessageACBase._message_serial = 1
        self._message_id = MessageACBase._message_serial

    @property
    def _body(self) -> bytearray:
        raise NotImplementedError

    @property
    def body(self) -> bytearray:
        """AC message base body."""
        body = bytearray([self.body_type]) + self._body + bytearray([self._message_id])
        body.append(calculate(body))
        return body

    def is_response(self, frame: bytes) -> bool:
        """Return whether a decrypted frame is the reply to this request."""
        if self._response_body_type is None:
            return True
        return (
            len(frame) > MessageHeaderAA.HEADER_LENGTH
            and frame[0] == ListTypes.AA
            and frame[AA_MESSAGE_TYPE_INDEX] == self.message_type
            and frame[MessageHeaderAA.HEADER_LENGTH] == self._response_body_type
        )


class MessageA0Query(MessageACBase):
    """AC message query(queryType == "a0_query")."""

    def __init__(self, protocol_version: int) -> None:
        """Initialize AC message query."""
        super().__init__(
            protocol_version=protocol_version,
            message_type=MessageType.query,
            body_type=ListTypes.A0,
        )

    @property
    def _body(self) -> bytearray:
        """Head 0x41 + query_body + _message_id + crc."""
        query_body = bytearray(1)
        query_body[0] = 0xA7
        return query_body


class MessageA0LongQuery(MessageACBase):
    """AC message query(queryType == "a0_query_long")."""

    def __init__(self, protocol_version: int) -> None:
        """Initialize AC message query."""
        super().__init__(
            protocol_version=protocol_version,
            message_type=MessageType.query,
            body_type=ListTypes.A0,
        )

    @property
    def _body(self) -> bytearray:
        """Head 0x41 + query_body + _message_id + crc."""
        return bytearray(19)


class MessageQuery(MessageACBase):
    """AC message query(queryType == nil)."""

    _response_body_type = ListTypes.C0

    def __init__(self, protocol_version: int) -> None:
        """Initialize AC message query."""
        super().__init__(
            protocol_version=protocol_version,
            message_type=MessageType.query,
            body_type=ListTypes.X41,
        )

    @property
    def _body(self) -> bytearray:
        """Head 0x41 + query_body + _message_id + crc."""
        query_body = bytearray(19)
        query_body[0] = 0x81
        query_body[2] = 0xFF
        return query_body


class MessageCapabilitiesQuery(MessageACBase):
    """AC message capabilities query(queryType == "all_first_frame")."""

    _response_body_type = ListTypes.B5

    def __init__(
        self,
        protocol_version: int,
        additional_capabilities: bool = False,
    ) -> None:
        """Initialize AC message capabilities query."""
        super().__init__(
            protocol_version=protocol_version,
            message_type=MessageType.query,
            body_type=ListTypes.B5,
        )
        self._additional_capabilities = additional_capabilities

    @property
    def _body(self) -> bytearray:
        if self._additional_capabilities:
            return bytearray([0x01, 0x01, 0x01])
        return bytearray([0x01, 0x00])


class MessageCapabilitiesAdditionalQuery(MessageCapabilitiesQuery):
    """AC message capabilities additional query(queryType == "all_second_frame")."""

    def __init__(
        self,
        protocol_version: int,
    ) -> None:
        """Initialize AC message capabilities additional query."""
        super().__init__(
            protocol_version=protocol_version,
            additional_capabilities=True,  # Always set to True for this class
        )


class MessageGroupDataQuery(MessageACBase):
    """AC message group data query(queryType == "group_data_<group>")."""

    _group = 0
    _response_body_type = ListTypes.C1

    def __init__(self, protocol_version: int) -> None:
        """Initialize AC message group data query."""
        super().__init__(
            protocol_version=protocol_version,
            message_type=MessageType.query,
            body_type=ListTypes.X41,
        )

    @property
    def _body(self) -> bytearray:
        return bytearray(
            [0x21, 0x01, XC1_GROUP_QUERY_BASE | self._group, 0x00, 0x01],
        )

    @property
    def body(self) -> bytearray:
        """AC message group data query body."""
        body = bytearray([self.body_type]) + self._body
        body.append(calculate(body))
        return body

    def is_response(self, frame: bytes) -> bool:
        """Return whether a decrypted frame is the reply for this group."""
        # some devices clear the 0x40 bit of the group byte in the reply
        group_index = MessageHeaderAA.HEADER_LENGTH + 3
        return (
            super().is_response(frame)
            and len(frame) > group_index
            and frame[group_index] & 0x0F == self._group
        )


class MessageGroupZeroQuery(MessageGroupDataQuery):
    """AC message power query(queryType == "group_data_zero")."""

    _group = 0


class MessageGroupOneQuery(MessageGroupDataQuery):
    """AC message compressor query(queryType == "group_data_one")."""

    _group = 1


class MessageGroupTwoQuery(MessageGroupDataQuery):
    """AC message indoor fan query(queryType == "group_data_two")."""

    _group = 2


class MessageGroupThreeQuery(MessageGroupDataQuery):
    """AC message outdoor fan query(queryType == "group_data_three")."""

    _group = 3


class MessagePowerQuery(MessageGroupDataQuery):
    """AC message power query(queryType == "group_data_four")."""

    _group = 4


class MessageHumidityQuery(MessageGroupDataQuery):
    """AC message query indoor humidity(queryType == "group_data_five")."""

    _group = 5


class MessageGroupSevenQuery(MessageGroupDataQuery):
    """AC message compressor power query(queryType == "group_data_seven")."""

    _group = 7


class MessageToggleDisplay(MessageACBase):
    """AC message toggle display."""

    def __init__(self, protocol_version: int) -> None:
        """Initialize AC message toggle display."""
        super().__init__(
            protocol_version=protocol_version,
            message_type=MessageType.query,
            body_type=ListTypes.X41,
        )
        self.prompt_tone = False

    @property
    def _body(self) -> bytearray:
        prompt_tone = 0x40 if self.prompt_tone else 0
        return bytearray(
            [
                0x02 | prompt_tone,
                0x00,
                0xFF,
                0x02,
                0x00,
                0x02,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
            ],
        )


class MessageNewProtocolQuery(MessageACBase):
    """AC message new protocol query."""

    _response_body_type = ListTypes.B1

    # error_code_query is deliberately not queried by default: on some B1
    # devices, including it makes both this query and CapabilitiesQuery get
    # recorded as unsupported protocol, breaking status refresh entirely.
    _query_params: tuple[int, ...] = (
        NewProtocolTags.indirect_wind,
        NewProtocolTags.breezeless,
        NewProtocolTags.indoor_humidity,
        NewProtocolTags.screen_display,
        NewProtocolTags.fresh_air_1,
        NewProtocolTags.fresh_air_2,
        NewProtocolTags.wind_lr_angle,
        NewProtocolTags.wind_ud_angle,
        NewProtocolTags.out_silent,
        NewProtocolTags.buzzer_all,
    )

    def __init__(
        self,
        protocol_version: int,
        *,
        supports_rate_select: bool = False,
    ) -> None:
        """Initialize AC message new protocol query.

        `supports_rate_select` gates the rate_select (0x0048) query param on
        the device having advertised it via the B5 b5_electricity capability
        (tag 0x0216). Devices that don't report it never answer the query, so
        it's left out until support is confirmed.
        """
        super().__init__(
            protocol_version=protocol_version,
            message_type=MessageType.query,
            body_type=ListTypes.B1,
        )
        self._supports_rate_select = supports_rate_select

    @property
    def _body(self) -> bytearray:

        params = list(self._query_params)
        if self._supports_rate_select:
            params.append(NewProtocolTags.rate_select)

        _body = bytearray([len(params)])
        for param in params:
            _body.extend([param & 0xFF, param >> 8])
        return _body


class MessageNewProtocolSelfCleanQuery(MessageNewProtocolQuery):
    """AC message new protocol self-clean query.

    A device answers with an empty parameter list when a query carries a tag it
    does not support, which suppresses every other tag in the same request. The
    self-clean state is therefore asked for as an independent status group.
    """

    _query_params = (NewProtocolTags.self_clean,)


class MessageSubProtocol(MessageACBase):
    """AC message sub protocol."""

    def __init__(
        self,
        protocol_version: int,
        message_type: MessageType,
        subprotocol_query_type: int,
    ) -> None:
        """Initialize AC message sub protocol."""
        super().__init__(
            protocol_version=protocol_version,
            message_type=message_type,
            body_type=ListTypes.AA,
        )
        self._subprotocol_query_type = subprotocol_query_type

    @property
    def _subprotocol_body(self) -> bytes | bytearray:
        return bytes([])

    @property
    def body(self) -> bytearray:
        """AC message sub protocol body."""
        body = bytearray([self.body_type]) + self._body
        body.append(calculate(body))
        body.extend(self.checksum(body, self._checksum_type))
        return body

    @property
    def _body(self) -> bytearray:
        _subprotocol_body = self._subprotocol_body
        _body = bytearray(
            [
                6 + 2 + len(_subprotocol_body),
                0x00,
                0xFF,
                0xFF,
                self._subprotocol_query_type,
            ],
        )
        _body.extend(_subprotocol_body)
        return _body


class MessageSubProtocolQuery(MessageSubProtocol):
    """AC message sub protocol query."""

    def __init__(
        self,
        protocol_version: int,
        subprotocol_query_type: int,
    ) -> None:
        """Initialize AC message sub protocol query."""
        super().__init__(
            protocol_version=protocol_version,
            message_type=MessageType.query,
            subprotocol_query_type=subprotocol_query_type,
        )


class MessageSubProtocolQuery10(MessageSubProtocolQuery):
    """AC sub protocol indoor status query."""

    def __init__(self, protocol_version: int) -> None:
        """Initialize the indoor status query."""
        super().__init__(protocol_version, ListTypes.X10)


class MessageSubProtocolQuery11(MessageSubProtocolQuery):
    """AC sub protocol basic status query."""

    def __init__(self, protocol_version: int) -> None:
        """Initialize the basic status query."""
        super().__init__(protocol_version, ListTypes.X11)


class MessageSubProtocolQuery30(MessageSubProtocolQuery):
    """AC sub protocol outdoor status query."""

    def __init__(self, protocol_version: int) -> None:
        """Initialize the outdoor status query."""
        super().__init__(protocol_version, ListTypes.X30)


class MessageSubProtocolSet(MessageSubProtocol):
    """AC message sub protocol set."""

    def __init__(self, protocol_version: int) -> None:
        """Initialize AC message sub protocol set."""
        super().__init__(
            protocol_version=protocol_version,
            message_type=MessageType.set,
            subprotocol_query_type=0x20,
        )
        self.power = False
        self.mode = 0
        self.follow_me = False
        self.target_temperature = 20.0
        self.fan_speed = 102
        self.boost_mode = False
        self.aux_heating = False
        self.dry = False
        self.eco_mode = False
        self.sleep_mode = False
        self.sn8_flag = False
        self.timer = False
        self.prompt_tone = False

    @property
    def _subprotocol_body(self) -> bytearray:
        power = 0x01 if self.power else 0
        dry = 0x10 if self.power and self.dry else 0
        boost_mode = 0x20 if self.boost_mode else 0
        aux_heating = 0x40 if self.aux_heating else 0x80
        sleep_mode = 0x80 if self.sleep_mode else 0
        follow_me = 0x40 if self.follow_me else 0
        try:
            mode = 0 if self.mode == 0 else BB_AC_MODES[self.mode] - 1
        except IndexError:
            mode = 2  # set Auto if invalid mode
        target_temperature = int(self.target_temperature * 2 + 30)
        water_model_temperature_set = int((self.target_temperature - 1) * 2 + 50)
        fan_speed = int(self.fan_speed)
        eco = 0x40 if self.eco_mode else 0

        prompt_tone = 0x01 if self.prompt_tone else 0
        timer = 0x04 if (self.sn8_flag and self.timer) else 0
        return bytearray(
            [
                0x02 | boost_mode | power | dry,
                aux_heating,
                sleep_mode,
                0x00,
                follow_me,
                mode,
                target_temperature,
                fan_speed,
                0x32,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x01,
                0x01,
                0x00,
                0x01,
                water_model_temperature_set,
                prompt_tone,
                target_temperature,
                0x32,
                0x66,
                0x00,
                eco | timer,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x08,
            ],
        )


class MessageSubProtocolFreshAirSet(MessageSubProtocol):
    """AC BB C0/02 single-control fresh-air command."""

    def __init__(
        self,
        protocol_version: int,
        power: bool,
        speed: int,
        *,
        exhaust: bool = False,
    ) -> None:
        """Initialize a fresh-air intake or exhaust command."""
        super().__init__(
            protocol_version=protocol_version,
            message_type=MessageType.set,
            subprotocol_query_type=ListTypes.C0,
        )
        self.power = power
        self.speed = max(1, min(speed, 100))
        self.exhaust = exhaust

    @property
    def _subprotocol_body(self) -> bytearray:
        body = bytearray(BB_FRESH_AIR_CONTROL_BODY_LENGTH)
        # BB fresh-air controls are sparse C0/02 payloads. The switch bytes are
        # relative to the control cursor; speed bytes are offsets in that block.
        body[1] = 0x01
        body[2] = 0x01
        body[11] = 0x01
        body[12] = ListTypes.C0
        body[13] = 0x02
        body[14] = 0x54
        switch_mask = (
            BB_FRESH_AIR_EXHAUST_SWITCH_MASK
            if self.exhaust
            else BB_FRESH_AIR_INTAKE_SWITCH_MASK
        )
        speed_index = (
            BB_FRESH_AIR_EXHAUST_CONTROL_SPEED_INDEX
            if self.exhaust
            else BB_FRESH_AIR_INTAKE_CONTROL_SPEED_INDEX
        )
        body[BB_FRESH_AIR_CONTROL_CURSOR + 6] = switch_mask
        body[BB_FRESH_AIR_CONTROL_CURSOR + 7] = switch_mask if self.power else 0
        body[BB_FRESH_AIR_CONTROL_CURSOR + speed_index] = (
            BB_FRESH_AIR_SPEED_FLAG | self.speed
        )
        body[-1] = (-sum(body[12:-1])) & MAX_BYTE_VALUE
        return body


class MessageSubProtocolResetFilter(MessageSubProtocol):
    """AC message reset filter."""

    def __init__(self, protocol_version: int) -> None:
        """Initialize AC message reset filter."""
        super().__init__(
            protocol_version,
            message_type=MessageType.set,
            subprotocol_query_type=ListTypes.AB,
        )

    @property
    def _subprotocol_body(self) -> bytearray:
        return bytearray([0x40 | 0x80, 0x01])


class MessageGeneralSet(MessageACBase):
    """AC message general set."""

    def __init__(self, protocol_version: int) -> None:
        """Initialize AC message general set."""
        super().__init__(
            protocol_version=protocol_version,
            message_type=MessageType.set,
            body_type=ListTypes.X40,
        )
        self.power = False
        self.prompt_tone = True
        self.mode = 0
        self.target_temperature = 20.0
        self.fan_speed = 102
        self.swing_vertical = False
        self.swing_horizontal = False
        self.boost_mode = False
        self.follow_me = False
        self.power_saving = False
        self.smart_eye = False
        self.dry = False
        self.aux_heating = False
        self.eco_mode = False
        self.temp_fahrenheit = False
        self.sleep_mode = False
        self.natural_wind = False
        self.frost_protect = False
        self.comfort_mode = False
        self.anion = False
        self.reset_filter = False

    @property
    def _body(self) -> bytearray:
        # Byte1, Power, prompt_tone
        power = 0x01 if self.power else 0
        prompt_tone = 0x40 if self.prompt_tone else 0
        # Byte2, mode target_temperature
        mode = (self.mode << 5) & 0xE0
        target_temperature = (int(self.target_temperature) & 0xF) | (
            0x10 if round(self.target_temperature * 2) % 2 != 0 else 0
        )
        # Byte 3, fan_speed
        fan_speed = int(self.fan_speed) & 0x7F
        # Byte 7, swing_mode
        swing_mode = (
            0x30
            | (0x0C if self.swing_vertical else 0)
            | (0x03 if self.swing_horizontal else 0)
        )
        # Byte 8, turbo, power saving
        boost_mode = 0x20 if self.boost_mode else 0
        follow_me = 0x80 if self.follow_me else 0
        power_saving = POWER_SAVING_VALUE if self.power_saving else 0
        # Byte 9 aux_heating eco_mode
        smart_eye = 0x01 if self.smart_eye else 0
        dry = 0x04 if self.dry else 0
        aux_heating = 0x08 if self.aux_heating else 0
        eco_mode = 0x80 if self.eco_mode else 0
        anion = 0x20 if self.anion else 0
        # Byte 10 temp_fahrenheit
        temp_fahrenheit = 0x04 if self.temp_fahrenheit else 0
        sleep_mode = 0x01 if self.sleep_mode else 0
        boost_mode_1 = 0x02 if self.boost_mode else 0
        common_filter_reset = 0x80 if self.reset_filter else 0
        # Byte 17 natural_wind
        natural_wind = 0x40 if self.natural_wind else 0
        # Byte 21 frost_protect
        frost_protect = 0x80 if self.frost_protect else 0
        # Byte 22 comfort_mode
        comfort_mode = 0x01 if self.comfort_mode else 0
        fresh_filter_reset = 0x08 if self.reset_filter else 0

        return bytearray(
            [
                power | prompt_tone,
                mode | target_temperature,
                fan_speed,
