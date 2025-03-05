import enum
from typing import Optional, Callable

class SpotflowError(Exception):
    pass

class ProvisioningOperation:
    def __init__(self) -> None: ...

    @property
    def id(self) -> str: ...

    @property
    def verification_code(self) -> str: ...

    @property
    def expiration_time(self) -> str: ...

class Compression(enum.Enum):
    UNCOMPRESSED = 0
    FASTEST = 1
    SMALLEST_SIZE = 2

class DeviceClient:
    @staticmethod
    def start(device_id: Optional[str],
              provisioning_token: str,
              db: str,
              instance: Optional[str] = None,
              display_provisioning_operation_callback: Optional[Callable[[ProvisioningOperation], None]] = None,
              desired_properties_updated_callback: Optional[Callable[[DesiredProperties], None]] = None,
              allow_remote_access: bool = False) -> DeviceClient:
         ...

    @property
    def workspace_id(self) -> str: ...

    @property
    def device_id(self) -> str: ...

    def create_stream_sender(self,
                             stream_group: Optional[str] = None,
                             stream: Optional[str] = None,
                             compression: Optional[Compression] = Compression.UNCOMPRESSED) -> StreamSender:
        ...

    @property
    def pending_messages_count(self) -> int: ...

    def wait_enqueued_messages_sent(self) -> None: ...

    def get_desired_properties(self) -> DesiredProperties: ...

    def get_desired_properties_if_newer(self, version: Optional[int] = None) -> Optional[DesiredProperties]: ...

    def update_reported_properties(self, properties: dict) -> None: ...

    @property
    def any_pending_reported_properties_updates(self) -> bool: ...

class StreamSender:
    def send_message(self, 
                     payload: str | bytes,
                     batch_id: Optional[str] = None,
                     message_id: Optional[str] = None,
                     batch_slice_id: Optional[str] = None,
                     chunk_id: Optional[str] = None) -> None:
        ...

    def enqueue_message(self, 
                     payload: str | bytes,
                     batch_id: Optional[str] = None,
                     message_id: Optional[str] = None,
                     batch_slice_id: Optional[str] = None,
                     chunk_id: Optional[str] = None) -> None:
        ...

    def enqueue_batch_completion(self, batch_id: str) -> None: ...

    def enqueue_message_completion(self, batch_id: str, message_id: str) -> None: ...

class DesiredProperties:
    @property
    def version(self) -> int: ...

    @property
    def values(self) -> dict: ...
