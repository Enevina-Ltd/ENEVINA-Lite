"""Midea Local device test."""

import asyncio
import contextlib
from collections.abc import Callable
from types import SimpleNamespace
from typing import Any, ClassVar
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from midealocal.cloud import DEFAULT_KEYS
from midealocal.const import DeviceType, ProtocolVersion
from midealocal.device import (
    MESSAGE_TYPE_INDEX,
    RESPONSE_TIMEOUT,
    SKIP_ATTRIBUTE,
    AuthException,
    MessageResult,
    MideaDevice,
    NoSupportedProtocol,
    ResponseException,
    dict_translator,
    list_translator,
    multiplier_translator,
    precision_halves_translator,
    sentinel_translator,
)
from midealocal.devices.ac import DeviceAttributes as ACAttributes
from midealocal.exceptions import SocketException
from midealocal.message import MessageType
from tests.conftest import make_stream_pair, running_reader_loop


class _DictDevice(MideaDevice):
    """MideaDevice subclass exposing a class-level dict for lookup tests."""

    modes: ClassVar[dict[int, str]] = {1: "auto", 2: "cool"}


def _skip_attribute_translator(_: int) -> Any:  # noqa: ANN401
    return SKIP_ATTRIBUTE


@pytest.mark.parametrize(
    ("values", "kwargs", "index", "expected"),
    [
        pytest.param(["a", "b", "c"], {}, 1, "b", id="in_range"),
        pytest.param(["a", "b", "c"], {}, 5, None, id="out_of_range_defaults_to_none"),
        pytest.param(
            ["a", "b", "c"],
            {},
            -1,
            None,
            id="negative_index_does_not_wrap",
        ),
        pytest.param(
            ["a", "b", "c"],
            {"default": "unknown"},
            5,
            "unknown",
            id="custom_default",
        ),
        pytest.param(
            ["a", "b", "c"],
            {"offset": 1},
            1,
            "a",
            id="offset_shifts_raw_value",
        ),
        pytest.param(
            ["a", "b", "c"],
            {"min_index": 1},
            0,
            None,
            id="min_index_excludes_low_index",
        ),
        pytest.param(
            ["a", "b", "c"],
            {"key": lambda v: v // 10},
            20,
            "c",
            id="key_transforms_raw_value_first",
        ),
    ],
)
def test_list_translator(
    values: list[str],
    kwargs: dict[str, Any],
    index: int,
    expected: str | None,
) -> None:
    """Test list_translator's offset, min_index, default, and key arguments."""
    assert list_translator(values, **kwargs)(index) == expected


def test_dict_translator_found_returns_mapped_value() -> None:
    """Test dict_translator looks up a present key in the mapping."""
    assert dict_translator({1: "a", 2: "b"})(1) == "a"


def test_dict_translator_not_found_passes_value_through_by_default() -> None:
    """Test dict_translator with no default passes an absent key through."""
    assert dict_translator({1: "a"})(99) == 99


@pytest.mark.parametrize(
    "default",
    [
        pytest.param("unknown", id="explicit_string_default"),
        pytest.param(None, id="explicit_none_default_distinct_from_unset"),
    ],
)
def test_dict_translator_not_found_uses_explicit_default(
    default: Any,  # noqa: ANN401
) -> None:
    """Test dict_translator returns an explicit default for an absent key."""
    assert dict_translator({1: "a"}, default=default)(99) == default


@pytest.mark.parametrize(
    ("precision_halves", "value", "expected"),
    [
        pytest.param(True, 10, 5, id="halves_when_enabled"),
        pytest.param(False, 10, 10, id="passes_through_when_disabled"),
        pytest.param(None, 10, 10, id="passes_through_when_unset"),
    ],
)
def test_precision_halves_translator(
    precision_halves: bool | None,
    value: float,
    expected: float,
) -> None:
    """Test precision_halves_translator's enabled, disabled, and unset states."""
    assert precision_halves_translator(precision_halves)(value) == expected


@pytest.mark.parametrize(
    ("multiplier", "value", "expected"),
    [
        pytest.param(3.0, 2, 6, id="scales_and_rounds"),
        pytest.param(1.0, 2, 2, id="no_op_multiplier_skips_rounding"),
        pytest.param(3.0, None, None, id="none_value_passes_through"),
    ],
)
def test_multiplier_translator(
    multiplier: float,
    value: float | None,
    expected: float | None,
) -> None:
    """Test multiplier_translator's scaling, no-op, and None-value arguments."""
    assert multiplier_translator(multiplier)(value) == expected


@pytest.mark.parametrize(
    ("sentinel", "replacement", "value", "expected"),
    [
        pytest.param(0xFF, None, 0xFF, None, id="sentinel_value_is_replaced"),
        pytest.param(0xFF, None, 5, 5, id="other_values_pass_through"),
    ],
)
def test_sentinel_translator(
    sentinel: int,
    replacement: Any,  # noqa: ANN401
    value: int,
    expected: Any,  # noqa: ANN401
) -> None:
    """Test sentinel_translator's replacement and passthrough arguments."""
    assert sentinel_translator(sentinel, replacement)(value) == expected


def test_get_dict_key_by_value() -> None:
    """Test get_dict_key_by_value found, not-found and missing-dict cases."""
    assert _DictDevice.get_dict_key_by_value("modes", "cool") == 2
    assert _DictDevice.get_dict_key_by_value("modes", "unknown") is None
    with pytest.raises(ValueError, match="does not have a dict named 'missing'"):
        _DictDevice.get_dict_key_by_value("missing", "cool")


def test_fetch_v2_message() -> None:
    """Test fetch v2 message."""
    assert MideaDevice.fetch_v2_message(bytes([])) == ([], bytes([]))
    assert MideaDevice.fetch_v2_message(bytes([0x1])) == ([], bytes([0x1]))
    with pytest.raises(ValueError, match="V2 frame length"):
        MideaDevice.fetch_v2_message(bytes([0x1] * 5 + [0x0] + [0x1] * 7))


def test_pre_process_message_short_message() -> None:
    """Test pre process message ignores messages shorter than the header."""
    # Some devices answer a query with fewer bytes than the 10-byte header
    # (observed: a 4-byte `01000000` from a 0xFA tower fan). Indexing the
    # message-type byte blindly raises IndexError, which aborts the whole
    # status parse, so short messages must be ignored instead.
    device = MideaDevice(
        name="Test Device",
        device_id=1,
        device_type=DeviceType.AC,
        ip_address="192.168.1.100",
        port=6444,
        token=DEFAULT_KEYS[99]["token"],
        key=DEFAULT_KEYS[99]["key"],
        device_protocol=ProtocolVersion.V3,
        model="test_model",
        subtype=1,
        attributes={},
        mac="1234567890ab",
    )
    for length in range(MESSAGE_TYPE_INDEX + 1):
        assert device.pre_process_message(bytearray([0x0] * length)) is False
        assert device._appliance_query is True


def test_parse_message_short_appliance_query_message_skips_process_message() -> None:
    """Test short appliance query messages are not processed as device status."""
    device = MideaDevice(
        name="Test Device",
        device_id=1,
        device_type=DeviceType.AC,
        ip_address="192.168.1.100",
        port=6444,
        token=DEFAULT_KEYS[99]["token"],
        key=DEFAULT_KEYS[99]["key"],
        device_protocol=ProtocolVersion.V3,
        model="test_model",
        subtype=1,
        attributes={},
        mac="1234567890ab",
    )
    encrypted_message = bytearray([0x0] * 72)
    encrypted_message[4] = 72
    with (
        patch.object(
            device._security,
            "decode_8370",
            return_value=([encrypted_message], b""),
        ),
        patch.object(
            device._security,
            "aes_decrypt",
            return_value=bytearray([0x01, 0x00, 0x00, 0x00]),
        ),
        patch.object(device, "process_message") as process_message_mock,
    ):
        assert device.parse_message(bytes([])) == MessageResult.SUCCESS

    process_message_mock.assert_not_called()
    assert device._appliance_query is True


@pytest.mark.parametrize(
    ("is_response", "expected"),
    [
        pytest.param(True, MessageResult.SUCCESS, id="reply"),
        pytest.param(False, MessageResult.UNMATCHED, id="other_frame"),
    ],
)
def test_parse_message_matches_pending_query(
    is_response: bool,
    expected: MessageResult,
) -> None:
    """A frame only answers the pending query when the query accepts it."""
    device = MideaDevice(
        name="Test Device",
        device_id=1,
        device_type=DeviceType.AC,
        ip_address="192.168.1.100",
        port=6444,
        token=DEFAULT_KEYS[99]["token"],
        key=DEFAULT_KEYS[99]["key"],
        device_protocol=ProtocolVersion.V3,
        model="test_model",
        subtype=1,
        attributes={},
        mac="1234567890ab",
    )
    device._appliance_query = False
    device._pending_query = MagicMock()
    device._pending_query.is_response.return_value = is_response
    encrypted_message = bytearray([0x0] * 72)
    encrypted_message[4] = 72
    decrypted = bytearray([0xAA] + [0x00] * 15)
    with (
        patch.object(
            device._security,
            "decode_8370",
            return_value=([encrypted_message], b""),
        ),
        patch.object(device._security, "aes_decrypt", return_value=decrypted),
        patch.object(device, "process_message", return_value={"key": 1}),
        patch.object(device, "_should_run", return_value=True),
        patch.object(device, "update_all") as update_all_mock,
    ):
        assert device.parse_message(bytes([])) == expected

    device._pending_query.is_response.assert_called_once_with(bytes(decrypted))
    # a frame for another query still updates the device status
    update_all_mock.assert_called_once_with({"key": 1})


# A valid V3 handshake response: header + 32 zero bytes + a 32-byte payload
# whose exact content is irrelevant to authenticate() beyond its length.
_AUTH_HANDSHAKE_RESPONSE = bytearray(
    [0x83, 0x70, 0, 64, 0x20, 1, 0, 0]
    + [0x00] * 32
    + [
        0xCE,
        0x8C,
        0xFB,
        0xF1,
        0x65,
        0x90,
        0xD1,
        0x07,
        0x6D,
        0xF8,
        0x3A,
        0x3B,
        0x67,
        0xCC,
        0x6B,
        0xB6,
        0x80,
        0xF6,
        0x0E,
        0x3D,
        0xFF,
        0xE7,
        0x74,
        0x92,
        0x14,
        0x4D,
        0xE9,
        0xD2,
        0xD5,
        0x74,
        0x7E,
        0x6F,
    ],
)


class TestMideaDevice:
    """Midea device test case."""

    device: MideaDevice

    @pytest.fixture(autouse=True)
    def _setup_device(self) -> None:
        """Midea A1 Device setup."""
        self.device = MideaDevice(
            name="Test Device",
            device_id=1,
            device_type=DeviceType.AC,
            ip_address="192.168.1.100",
            port=6444,
            token=DEFAULT_KEYS[99]["token"],
            key=DEFAULT_KEYS[99]["key"],
            device_protocol=ProtocolVersion.V3,
            model="test_model",
            subtype=1,
            attributes={},
            mac="1234567890ab",
            serial_number="test_serial",
            manufacturer_code="1234",
        )

    def test_initial_attributes(self) -> None:
        """Test initial attributes."""
        assert len(self.device.attributes) == 0
        assert self.device.name == "Test Device"
        assert self.device.device_id == 1
        assert self.device.device_type == 0xAC
        assert self.device.model == "test_model"
        assert self.device.subtype == 1
        assert self.device.mac == "1234567890ab"
        assert self.device.serial_number == "test_serial"
        assert self.device.manufacturer_code == "1234"

    @pytest.mark.parametrize(
        ("serial", "expected_manufacturer_code"),
        [
            ("anything", "0000"),
            ("ABC1234567800000000000", "0000"),
            ("00000000012345678abbccddeeff0000", "0000"),
            ("00230000012345678abbccddeeff0000", "0023"),
            ("00080000012345678abbccddeeff0000", "0008"),
        ],
    )
    def test_device_manufacturer_code_by_serial(
        self,
        serial: str,
        expected_manufacturer_code: str,
    ) -> None:
        """Test device manutacturer_code based on the serial."""
        device = MideaDevice(
            name="Test Device",
            device_id=1,
            device_type=DeviceType.AC,
            ip_address="192.168.1.100",
            port=6444,
            token=DEFAULT_KEYS[99]["token"],
            key=DEFAULT_KEYS[99]["key"],
            device_protocol=ProtocolVersion.V3,
            model="test_model",
            subtype=1,
            attributes={},
            mac="1234567890ab",
            serial_number=serial,
        )
        assert device.manufacturer_code == expected_manufacturer_code

    def test_get_attribute(self) -> None:
        """Test get_attribute reads from the internal attributes dict."""
        self.device._attributes["power"] = True
        assert self.device.get_attribute("power") is True
        assert self.device.get_attribute("missing") is None

    def test_attributes_property(self) -> None:
        """Test attributes property stringifies keys from the internal dict."""
        self.device._attributes[ACAttributes.power] = True
        assert self.device.attributes == {str(ACAttributes.power): True}

    @pytest.mark.parametrize(
        (
            "initial_value",
            "message_kwargs",
            "translators",
            "default_transform",
            "expected_status",
            "expected_attribute",
        ),
        [
            pytest.param(
                None,
                {"mode": 2},
                {"mode": lambda v: f"translator-{v}"},
                lambda v: f"default-{v}",
                {"mode": "translator-2"},
                "translator-2",
                id="translator_wins_over_default_transform",
            ),
            pytest.param(
                None,
                {"mode": 2},
                None,
                lambda v: f"default-{v}",
                {"mode": "default-2"},
                "default-2",
                id="default_transform_used_when_no_translator",
            ),
            pytest.param(
                "previous",
                {},
                None,
                None,
                {},
                "previous",
                id="missing_field_is_ignored",
            ),
            pytest.param(
                None,
                {"mode": 2},
                None,
                None,
                {"mode": 2},
                2,
                id="status_and_attributes_stay_synchronized",
            ),
            pytest.param(
                "previous",
                {"mode": 2},
                {"mode": _skip_attribute_translator},
                None,
                {},
                "previous",
                id="skip_attribute_preserves_stored_value",
            ),
        ],
    )
    def test_update_attributes_from_message(
        self,
        initial_value: Any,  # noqa: ANN401
        message_kwargs: dict[str, Any],
        translators: dict[str, Callable[[Any], Any]] | None,
        default_transform: Callable[[Any], Any] | None,
        expected_status: dict[str, Any],
        expected_attribute: Any,  # noqa: ANN401
    ) -> None:
        """Test translator precedence, defaults, missing fields, and SKIP_ATTRIBUTE."""
        self.device._attributes = {"mode": initial_value}
        message = SimpleNamespace(**message_kwargs)
        new_status = self.device.update_attributes_from_message(
            message,
            translators=translators,
            default_transform=default_transform,
        )
        assert new_status == expected_status
        assert self.device.attributes["mode"] == expected_attribute

    def test_celsius_to_fahrenheit(self) -> None:
        """Test celsius_to_fahrenheit conversion and pass-through branches."""
        assert self.device.celsius_to_fahrenheit(20, is_fahrenheit=True) == 68
        assert self.device.celsius_to_fahrenheit(20, is_fahrenheit=False) == 20
        # is_fahrenheit=None falls back to the class default (False), so the
        # value passes through unconverted.
        assert self.device.celsius_to_fahrenheit(20) == 20

    def test_fahrenheit_to_celsius(self) -> None:
        """Test fahrenheit_to_celsius conversion and pass-through branches."""
        assert self.device.fahrenheit_to_celsius(68, is_fahrenheit=True) == 20
        assert self.device.fahrenheit_to_celsius(68, is_fahrenheit=False) == 68
        assert self.device.fahrenheit_to_celsius(68) == 68

    @pytest.mark.parametrize(
        ("exc", "result"),
        [
            (TimeoutError, False),
            (OSError, False),
            (AuthException, False),
            (NoSupportedProtocol, False),
            (SocketException, False),
            (None, True),
        ],
    )
    @pytest.mark.asyncio
    async def test_connect(
        self,
        exc: type[Exception] | None,
        result: bool,
    ) -> None:
        """Test connect."""
        # Pre-populate buffer to confirm the failure path runs close_socket(),
        # which clears it.
        self.device._buffer = b"stale"
        reader, writer = make_stream_pair([b""])
        open_connection_mock = AsyncMock(side_effect=exc, return_value=(reader, writer))
        with (
            patch("midealocal.device.asyncio.open_connection", open_connection_mock),
            patch.object(self.device, "authenticate", new=AsyncMock()),
            patch.object(self.device, "refresh_status", new=AsyncMock()),
        ):
            assert await self.device.connect(check_protocol=True) is result
            assert self.device.available is result
            assert (self.device._writer is None) is (not result)
            if not result:
                # close_socket() was invoked: it also resets the buffer.
                assert self.device._buffer == b""
        # connect() starts a reader task on the successful path; stop it.
        task, self.device._reader_task = self.device._reader_task, None
        if task is not None:
            task.cancel()
            with contextlib.suppress(BaseException):
                await task

    @pytest.mark.asyncio
    async def test_connect_v2_skips_authenticate(self) -> None:
        """Test a V2 device connects without the V3 handshake."""
        self.device._device_protocol_version = ProtocolVersion.V2
        reader, writer = make_stream_pair()
        with patch(
            "midealocal.device.asyncio.open_connection",
            AsyncMock(return_value=(reader, writer)),
        ):
            assert await self.device.connect() is True
        writer.write.assert_not_called()
        await self.device.close_socket()

    @pytest.mark.asyncio
    async def test_connect_v3_does_not_race_authenticate_against_read_loop(
        self,
    ) -> None:
        """Regression test: the reader task must not start until after the V3 handshake.

        asyncio.StreamReader raises RuntimeError if two coroutines await
        read() on it concurrently. authenticate() does its own raw read()
        for the handshake response, so _read_loop() (the sole reader once
        the connection is up) must not start until that read is done --
        this uses a real StreamReader, since mocks don't reproduce that
        guard.
        """
        reader = asyncio.StreamReader()
        writer = MagicMock(spec=asyncio.StreamWriter)
        writer.wait_closed = AsyncMock()

        async def _feed_handshake_response() -> None:
            # Give the event loop a beat before the response "arrives", so
            # a reader task started too early has every chance to race
            # authenticate()'s own pending read() on self._reader.
            await asyncio.sleep(0)
            reader.feed_data(bytes(_AUTH_HANDSHAKE_RESPONSE))

        feeder = asyncio.create_task(_feed_handshake_response())
        with patch(
            "midealocal.device.asyncio.open_connection",
            new=AsyncMock(return_value=(reader, writer)),
        ):
            assert await self.device.connect() is True
        await feeder

        task, self.device._reader_task = self.device._reader_task, None
        assert task is not None
        # A task that already crashed (e.g. RuntimeError from a concurrent
        # read()) finished before cancel() below has any effect on it, so
        # it comes back non-cancelled -- that's the failure this catches.
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await task
        assert task.cancelled()

    @pytest.mark.asyncio
    async def test_connect_generic_exception(self) -> None:
        """Test connect with generic exception."""
        self.device._buffer = b"stale"
        with patch(
            "midealocal.device.asyncio.open_connection",
            new=AsyncMock(side_effect=Exception()),
        ):
            assert await self.device.connect() is False
            assert self.device.available is False
            assert self.device._writer is None
            assert self.device._buffer == b""

    @pytest.mark.asyncio
    async def test_authenticate(self) -> None:
        """Test authenticate."""
        reader, writer = make_stream_pair()
        reader.readexactly.side_effect = [
            asyncio.IncompleteReadError(b"", 8),
            _AUTH_HANDSHAKE_RESPONSE[:8],
            _AUTH_HANDSHAKE_RESPONSE[8:],
        ]

        with pytest.raises(SocketException):
            await self.device.authenticate()

        self.device._reader, self.device._writer = reader, writer
        with pytest.raises(AuthException):
            await self.device.authenticate()

        await self.device.authenticate()

    @pytest.mark.asyncio
    async def test_send_message(self) -> None:
        """Test send message."""
        reader, writer = make_stream_pair()
        reader.readexactly.side_effect = [
            _AUTH_HANDSHAKE_RESPONSE[:8],
            _AUTH_HANDSHAKE_RESPONSE[8:],
        ]
        self.device._reader, self.device._writer = reader, writer
        await self.device.authenticate()
        self.device.send_message(bytes([0x0] * 20))
        self.device._device_protocol_version = ProtocolVersion.V2
        self.device.send_message(bytes([0x0] * 20))

    @pytest.mark.parametrize(
        "closing_writer",
        [
            pytest.param(False, id="writer_none"),
            pytest.param(True, id="writer_closing"),
        ],
    )
    def test_send_message_v2_no_usable_writer(self, closing_writer: bool) -> None:
        """Test send_message_v2 raises SocketException without a usable writer.

        A closing StreamWriter drops writes silently, so it must be rejected
        up front just like a missing one.
        """
        _reader, writer = make_stream_pair()
        writer.is_closing.return_value = True
        self.device._writer = writer if closing_writer else None
        with pytest.raises(SocketException):
            self.device.send_message_v2(bytes([0x0] * 20))

    @pytest.mark.parametrize(
        "exc",
        [TimeoutError, ConnectionResetError, OSError, ValueError],
    )
    def test_send_message_v2_send_errors_reraised(self, exc: type[Exception]) -> None:
        """Test send_message_v2 propagates every writer.write failure."""
        _reader, writer = make_stream_pair()
        writer.write.side_effect = exc("boom")
        self.device._writer = writer
        with pytest.raises(exc):
            self.device.send_message_v2(bytes([0x0] * 20))

    def test_build_send(self) -> None:
        """Test build_send serializes, packages and sends the command."""
        cmd = MagicMock()
        cmd.serialize.return_value = bytes([0x01, 0x02])
        with patch.object(self.device, "send_message") as send_mock:
            self.device.build_send(cmd)
        cmd.serialize.assert_called_once()
        send_mock.assert_called_once()

    @pytest.mark.asyncio
    async def test_wait_for_query_response_no_reader_task_raises_socket_exception(
        self,
    ) -> None:
        """Test _wait_for_query_response with no reader task running."""
        self.device._reader_task = None
        with pytest.raises(SocketException):
            await self.device._wait_for_query_response(MagicMock())

    @pytest.mark.asyncio
    @pytest.mark.parametrize(
        ("reader_exc", "expected", "expected_cause"),
        [
            pytest.param(None, SocketException, type(None), id="no_exception"),
            pytest.param(
                ResponseException(),
                SocketException,
                ResponseException,
                id="unsolicited_error_frame",
            ),
            pytest.param(
                ConnectionResetError(),
                ConnectionResetError,
                type(None),
                id="connection_reset",
            ),
            pytest.param(
                ValueError("bug"),
                ValueError,
                type(None),
                id="unexpected_error",
            ),
        ],
    )
    async def test_wait_for_query_response_done_reader_task(
        self,
        reader_exc: Exception | None,
        expected: type[Exception],
        expected_cause: type[BaseException | None],
    ) -> None:
        """Test _wait_for_query_response surfaces why the reader task exited."""

        async def _reader() -> None:
            if reader_exc is not None:
                raise reader_exc

        task = asyncio.create_task(_reader())
        await asyncio.wait({task})
        self.device._reader_task = task
        with pytest.raises(expected) as exc_info:
            await self.device._wait_for_query_response(MagicMock())
        assert type(exc_info.value.__cause__) is expected_cause

    @pytest.mark.asyncio
    async def test_wait_for_query_response_cancelled_reader_task(self) -> None:
        """Test _wait_for_query_response once the reader task was cancelled."""
        task = asyncio.create_task(asyncio.sleep(100))
        task.cancel()
        await asyncio.wait({task})
        self.device._reader_task = task
        with pytest.raises(SocketException):
            await self.device._wait_for_query_response(MagicMock())

    @pytest.mark.asyncio
    async def test_wait_for_query_response_times_out(self) -> None:
        """Test _wait_for_query_response raises TimeoutError and clears the waiter."""
        idle_task = asyncio.create_task(asyncio.sleep(100))
        self.device._reader_task = idle_task
        with (
            patch("midealocal.device.QUERY_TIMEOUT", 0.01),
            pytest.raises(TimeoutError),
        ):
            await self.device._wait_for_query_response(MagicMock())
        assert self.device._response_waiter is None
        idle_task.cancel()
        with contextlib.suppress(BaseException):
            await idle_task

    @pytest.mark.asyncio
    async def test_wait_for_query_response_resolves_when_waiter_is_set(self) -> None:
        """Test _wait_for_query_response returns once the waiter is resolved."""
        idle_task = asyncio.create_task(asyncio.sleep(100))
        self.device._reader_task = idle_task

        async def _resolve_soon() -> None:
            await asyncio.sleep(0)
            assert self.device._response_waiter is not None
            self.device._response_waiter.set_result(MessageResult.SUCCESS)

        resolver = asyncio.create_task(_resolve_soon())
        await self.device._wait_for_query_response(MagicMock())
        await resolver
        idle_task.cancel()
        with contextlib.suppress(BaseException):
            await idle_task

    @pytest.mark.asyncio
    async def test_resolve_waiter_or_raise_success_sets_result(self) -> None:
        """Test _resolve_waiter_or_raise resolves a pending waiter on SUCCESS."""
        waiter = asyncio.get_running_loop().create_future()
        self.device._response_waiter = waiter
        self.device._resolve_waiter_or_raise(MessageResult.SUCCESS)
        assert waiter.result() == MessageResult.SUCCESS

    @pytest.mark.asyncio
    async def test_resolve_waiter_or_raise_non_success_rejects_waiter(self) -> None:
        """Test _resolve_waiter_or_raise rejects a pending waiter on a bad result."""
        waiter = asyncio.get_running_loop().create_future()
        self.device._response_waiter = waiter
        self.device._resolve_waiter_or_raise(MessageResult.ERROR)
        with pytest.raises(ResponseException):
            waiter.result()

    @pytest.mark.asyncio
    async def test_resolve_waiter_or_raise_unsolicited_error_raises(self) -> None:
        """Test _resolve_waiter_or_raise raises when nobody is waiting."""
        self.device._response_waiter = None
        with pytest.raises(ResponseException):
            self.device._resolve_waiter_or_raise(MessageResult.ERROR)

    @pytest.mark.asyncio
    async def test_resolve_waiter_or_raise_ignores_already_done_waiter(self) -> None:
        """An already-resolved waiter must not swallow a later unsolicited error."""
        waiter = asyncio.get_running_loop().create_future()
        waiter.set_result(MessageResult.SUCCESS)
        self.device._response_waiter = waiter
        with pytest.raises(ResponseException):
            self.device._resolve_waiter_or_raise(MessageResult.ERROR)

    @pytest.mark.asyncio
    async def test_fail_waiter_sets_exception_on_pending_waiter(self) -> None:
        """Test _fail_waiter rejects a pending waiter with the given exception."""
        waiter = asyncio.get_running_loop().create_future()
        self.device._response_waiter = waiter
        exc = SocketException()
        self.device._fail_waiter(exc)
        assert waiter.exception() is exc

    def test_fail_waiter_noop_when_no_waiter(self) -> None:
        """Test _fail_waiter is a no-op when nothing is waiting."""
        self.device._response_waiter = None
        self.device._fail_waiter(SocketException())

    @pytest.mark.asyncio
    async def test_read_loop_requires_reader(self) -> None:
        """Test _read_loop raises SocketException when self._reader is None."""
        self.device._reader = None
        with pytest.raises(SocketException):
            await self.device._read_loop()

    @pytest.mark.asyncio
    async def test_read_loop_empty_data_raises_connection_reset(self) -> None:
        """Test _read_loop treats an empty read as the peer closing the connection."""
        reader, _writer = make_stream_pair([b""])
        self.device._reader = reader
        waiter = asyncio.get_running_loop().create_future()
        self.device._response_waiter = waiter
        with pytest.raises(ConnectionResetError, match=r"Connection closed by peer\."):
            await self.device._read_loop()
        with pytest.raises(ConnectionResetError):
            waiter.result()

    @pytest.mark.asyncio
    async def test_read_loop_read_error_propagates_and_fails_waiter(self) -> None:
        """Test _read_loop re-raises and fails the waiter on a read-level error."""
        reader, _writer = make_stream_pair([OSError("boom")])
        self.device._reader = reader
        waiter = asyncio.get_running_loop().create_future()
        self.device._response_waiter = waiter
        with pytest.raises(OSError, match="boom"):
            await self.device._read_loop()
        with pytest.raises(OSError, match="boom"):
            waiter.result()

    @pytest.mark.asyncio
    async def test_read_loop_parse_error_fails_waiter_and_propagates(self) -> None:
        """Test _read_loop re-raises and fails the waiter on a parse_message error."""
        reader, _writer = make_stream_pair([b"\x00"])
        self.device._reader = reader
        waiter = asyncio.get_running_loop().create_future()
        self.device._response_waiter = waiter
        with (
            patch.object(
                self.device,
                "parse_message",
                side_effect=ValueError("bad frame"),
            ),
            pytest.raises(ValueError, match="bad frame"),
        ):
            await self.device._read_loop()
        with pytest.raises(ValueError, match="bad frame"):
            waiter.result()

    @pytest.mark.asyncio
    async def test_read_loop_consecutive_timeouts_raise_socket_exception(self) -> None:
        """Test RESPONSE_TIMEOUT consecutive read timeouts tear down the connection."""
        reader, _writer = make_stream_pair([TimeoutError()] * RESPONSE_TIMEOUT)
        self.device._reader = reader
        waiter = asyncio.get_running_loop().create_future()
        self.device._response_waiter = waiter
        with pytest.raises(SocketException):
            await self.device._read_loop()
        with pytest.raises(SocketException):
            waiter.result()

    @pytest.mark.parametrize(
        "skipped",
        [
            pytest.param(MessageResult.PADDING, id="padding"),
            pytest.param(MessageResult.UNMATCHED, id="unmatched"),
        ],
    )
    @pytest.mark.asyncio
    async def test_read_loop_skipped_result_then_success_resolves_waiter(
        self,
        skipped: MessageResult,
    ) -> None:
        """Test _read_loop loops past results that don't answer the waiter."""
        reader, _writer = make_stream_pair([b"\x00", b"\x00"])
        self.device._reader = reader
        waiter = asyncio.get_running_loop().create_future()
        self.device._response_waiter = waiter
        with patch.object(
            self.device,
            "parse_message",
            side_effect=[skipped, MessageResult.SUCCESS],
        ):
            task = asyncio.create_task(self.device._read_loop())
            try:
                result = await asyncio.wait_for(waiter, timeout=1)
            finally:
                task.cancel()
                with contextlib.suppress(BaseException):
                    await task
        assert result == MessageResult.SUCCESS

    def test_request_refresh_sets_flag(self) -> None:
        """Test request_refresh sets the pending-refresh flag."""
        assert self.device._refresh_requested is False
        self.device.request_refresh()
        assert self.device._refresh_requested is True

    @pytest.mark.asyncio
    async def test_read_loop_services_pending_refresh_request(self) -> None:
        """Test _read_loop schedules refresh_status() when a refresh was requested."""
        reader, _writer = make_stream_pair([b"\x00", b""])
        self.device._reader = reader

        def _fake_parse(_data: bytes) -> MessageResult:
            self.device.request_refresh()
            return MessageResult.SUCCESS

        with (
            patch.object(self.device, "parse_message", side_effect=_fake_parse),
            patch.object(
                self.device,
                "refresh_status",
                new=AsyncMock(),
            ) as refresh_mock,
        ):
            with pytest.raises(ConnectionResetError):
                await asyncio.wait_for(self.device._read_loop(), timeout=1)
            await asyncio.sleep(0)  # let the detached refresh task run
            refresh_mock.assert_called_once()
        assert self.device._refresh_requested is False

    @pytest.mark.asyncio
    async def test_read_loop_does_not_block_on_requested_refresh(self) -> None:
        """A pending refresh must be scheduled, not awaited, by the read loop.

        Regression test: refresh_status() takes self._query_lock, which
        another coroutine can be holding while it awaits a response that
        only this same read loop, continuing on to its next read, can ever
        deliver. Awaiting the requested refresh inline here would deadlock
        that caller; the read loop must reach its next message regardless of
        how long the refresh takes.
        """
        reader, _writer = make_stream_pair([b"\x00", b""])
        self.device._reader = reader
        refresh_started = asyncio.Event()
        refresh_may_finish = asyncio.Event()

        def _fake_parse(_data: bytes) -> MessageResult:
            self.device.request_refresh()
            return MessageResult.SUCCESS

        async def _blocked_refresh() -> None:
            refresh_started.set()
            await refresh_may_finish.wait()

        with (
            patch.object(self.device, "parse_message", side_effect=_fake_parse),
            patch.object(
                self.device,
                "refresh_status",
                new=AsyncMock(side_effect=_blocked_refresh),
            ),
        ):
            read_loop_task = asyncio.create_task(self.device._read_loop())
            # If the refresh were awaited inline instead of scheduled as its
            # own task, the read loop could never reach EOF while it's
            # blocked awaiting refresh_may_finish below.
            with pytest.raises(ConnectionResetError):
                await asyncio.wait_for(read_loop_task, timeout=1)
            await asyncio.wait_for(refresh_started.wait(), timeout=1)
