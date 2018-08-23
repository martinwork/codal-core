#include "PktMessageBusDriver.h"
#include "CodalDmesg.h"

using namespace codal;

PktMessageBusDriver::PktMessageBusDriver(bool remote, uint32_t serial) :
    PktSerialDriver(PktDevice(0, 0, (remote) ? PKT_DEVICE_FLAGS_REMOTE : PKT_DEVICE_FLAGS_LOCAL, serial),
                    PKT_DRIVER_CLASS_MESSAGE_BUS,
                    DEVICE_ID_PKT_MESSAGE_BUS_DRIVER)
{
    suppressForwarding = false;
}

/**
  * Associates the given event with the serial channel.
  *
  * Once registered, all events matching the given registration sent to this micro:bit's
  * default EventModel will be automatically retransmitted on the serial bus.
  *
  * @param id The id of the event to register.
  *
  * @param value the value of the event to register.
  *
  * @return DEVICE_OK on success, or DEVICE_NO_RESOURCES if no default EventModel is available.
  *
  * @note The wildcards DEVICE_ID_ANY and DEVICE_EVT_ANY can also be in place of the
  *       id and value fields.
  */
int PktMessageBusDriver::listen(uint16_t id, uint16_t value)
{
    if (EventModel::defaultEventBus)
        return listen(id, value, *EventModel::defaultEventBus);

    return DEVICE_NO_RESOURCES;
}

/**
  * Associates the given event with the serial channel.
  *
  * Once registered, all events matching the given registration sent to the given
  * EventModel will be automatically retransmitted on the serial bus.
  *
  * @param id The id of the events to register.
  *
  * @param value the value of the event to register.
  *
  * @param eventBus The EventModel to listen for events on.
  *
  * @return DEVICE_OK on success.
  *
  * @note The wildcards DEVICE_ID_ANY and DEVICE_EVT_ANY can also be in place of the
  *       id and value fields.
  */
int PktMessageBusDriver::listen(uint16_t id, uint16_t value, EventModel &eventBus)
{
    return eventBus.listen(id, value, this, &PktMessageBusDriver::eventReceived, MESSAGE_BUS_LISTENER_IMMEDIATE);
}

/**
  * Disassociates the given event with the serial channel.
  *
  * @param id The id of the events to deregister.
  *
  * @param value The value of the event to deregister.
  *
  * @return DEVICE_OK on success, or DEVICE_INVALID_PARAMETER if the default message bus does not exist.
  *
  * @note DEVICE_EVT_ANY can be used to deregister all event values matching the given id.
  */
int PktMessageBusDriver::ignore(uint16_t id, uint16_t value)
{
    if (EventModel::defaultEventBus)
        return ignore(id, value, *EventModel::defaultEventBus);

    return DEVICE_INVALID_PARAMETER;
}

/**
  * Disassociates the given events with the serial channel.
  *
  * @param id The id of the events to deregister.
  *
  * @param value The value of the event to deregister.
  *
  * @param eventBus The EventModel to deregister on.
  *
  * @return DEVICE_OK on success.
  *
  * @note DEVICE_EVT_ANY can be used to deregister all event values matching the given id.
  */
int PktMessageBusDriver::ignore(uint16_t id, uint16_t value, EventModel &eventBus)
{
    return eventBus.ignore(id, value, this, &PktMessageBusDriver::eventReceived);
}


/**
  * Protocol handler callback. This is called when the serial bus receives a packet marked as using the event protocol.
  *
  * This function process this packet, and fires the event contained inside onto the default EventModel.
  */
void PktMessageBusDriver::handlePacket(PktSerialPkt* p)
{
    Event *e = (Event *) p->data;

    PKT_DMESG("EV: %d, %d", e->source, e->value);

    suppressForwarding = true;
    e->fire();
    suppressForwarding = false;
}

void PktMessageBusDriver::handleControlPacket(ControlPacket*)
{

}

/**
  * Event handler callback. This is called whenever an event is received matching one of those registered through
  * the registerEvent() method described above. Upon receiving such an event, it is wrapped into
  * a serial bus packet and transmitted to any other micro:bits in the same group.
  */
void PktMessageBusDriver::eventReceived(Event e)
{
    PKT_DMESG("EVENT");
    if(suppressForwarding)
        return;

    PKT_DMESG("PACKET QUEUED: %d %d", e.source, e.value);
    PktSerialProtocol::send((uint8_t *)&e, sizeof(Event), device.address);
}