#include "CodalConfig.h"
#include "JDControlLayer.h"
#include "CodalDmesg.h"
#include "Timer.h"

using namespace codal;

uint64_t generate_eui64(uint64_t device_identifier)
{
    uint64_t udid;

    udid = device_identifier;

    uint8_t* bytePtr = (uint8_t *)&udid;

    // set the address to locally administered (it wasn't assigned as globally unique, it's made up).
    // https://community.cisco.com/t5/networking-documents/understanding-ipv6-eui-64-bit-address/ta-p/3116953
    bytePtr[7] &= ~0x02;

    return udid;
}

void JDControlService::setConnectionState(bool state, JDDevice* device)
{
    // iterate over services on the device and provide connect / disconnect events.
    for (int i = 0; i < JD_PROTOCOL_SERVICE_ARRAY_SIZE; i++)
    {
        JDService* current = JDControlLayer::instance->services[i];

        // set device if host and device == this->device->
        if (current == NULL || current == this || current->device != device)
            continue;

        if (state)
        {
            current->device = device;
            current->hostConnected();
        }
        else
        {
            current->device = NULL;
            current->service_number = JD_SERVICE_UNITIALISED_VAL;
            current->hostDisconnected();
        }
    }
}

JDDevice* JDControlService::getRemoteDevice(uint8_t device_address)
{
    JDDevice* head = this->remoteDevices;

    while(head)
    {
        if (head->device_address == device_address)
            return head;

        head = head->next;
    }

    return NULL;
}

JDDevice* JDControlService::getRemoteDevice(uint8_t device_address, uint64_t udid)
{
    JDDevice* head = this->remoteDevices;

    while(head)
    {
        if (head->device_address == device_address && head->udid == udid)
            return head;

        head = head->next;
    }

    return NULL;
}

JDDevice* JDControlService::addRemoteDevice(JDControlPacket* remoteDevice, uint8_t communicationRate)
{
    JDDevice* newRemote = (JDDevice *) malloc(sizeof(JDDevice));

    newRemote->device_address = remoteDevice->device_address;
    newRemote->udid = remoteDevice->udid;
    newRemote->device_flags = remoteDevice->device_flags;
    newRemote->communication_rate = communicationRate;
    newRemote->rolling_counter = 0;
    newRemote->next = NULL;

    if (remoteDevice->device_flags & JD_DEVICE_FLAGS_HAS_NAME)
    {
        uint8_t len = *remoteDevice->data;
        newRemote->name = (uint8_t *)malloc(len + 1);
        memcpy(newRemote->name, remoteDevice->data + 1, len);
        newRemote->name[len] = 0;
    }

    if (this->remoteDevices == NULL)
        this->remoteDevices = newRemote;
    else
    {
        JDDevice* head = this->remoteDevices;

        while(head)
        {
            // guard against duplicates.
            if (head->device_address == newRemote->device_address && head->udid == device->udid)
            {
                free(newRemote);
                return head;
            }

            head = head->next;
        }

        head->next = newRemote;
    }

    return newRemote;
}

int JDControlService::removeRemoteDevice(JDDevice* device)
{
    if (this->remoteDevices == NULL)
        return DEVICE_INVALID_PARAMETER;

    JDDevice* curr = this->remoteDevices;
    JDDevice* prev = NULL;

    while (curr)
    {
        // found!!
        if (curr->device_address == device->device_address && curr->udid == device->udid)
        {
            if (prev == NULL)
                this->remoteDevices = curr->next;
            else
                prev->next = curr->next;

            return DEVICE_OK;
        }

        prev = curr;
        curr = curr->next;
    }

    return DEVICE_INVALID_PARAMETER;
}

int JDControlService::formControlPacket()
{
    uint16_t size = 0;

    enumerationData->udid = this->device->udid;
    enumerationData->device_address = this->device->device_address;
    enumerationData->device_flags = this->device->device_flags;

    // name change is only allowed when the device is re-enumerated.
    if (enumerationData->device_flags & JD_DEVICE_FLAGS_HAS_NAME)
        size += *enumerationData->data;

    // compile the list of host services for control packet
    JDServiceInformation* info = (JDServiceInformation *)(enumerationData->data + size);

    int service_number = 0;

    for (int i = 0; i < JD_PROTOCOL_SERVICE_ARRAY_SIZE; i++)
    {
        JDService* current = JDControlLayer::instance->services[i];

        if (current == NULL || current == this || current->mode != HostService)
            continue;

        // the device has modified its service numbers whilst enumerated.
        if (current->service_number != JD_SERVICE_UNITIALISED_VAL && current->service_number != service_number)
            target_panic(DEVICE_JACDAC_ERROR);

        current->service_number = service_number;

        info = (JDServiceInformation *)(enumerationData->data + size);

        info->service_flags = current->service_flags;
        info->service_class = current->service_class;
        info->advertisement_size = current->addAdvertisementData(info->data);

        size += info->advertisement_size + JD_SERVICE_INFO_HEADER_SIZE;
        service_number++;
    }

    CODAL_ASSERT((size + JD_CONTROL_PACKET_HEADER_SIZE) <= JD_MAX_PACKET_SIZE, DEVICE_JACDAC_ERROR);

    return size + JD_CONTROL_PACKET_HEADER_SIZE;
}

/**
 * Timer callback every 500 ms
 **/
void JDControlService::timerCallback(Event)
{
    // no sense continuing if we dont have a bus to transmit on...
    if (!JDControlLayer::instance || !JDControlLayer::instance->bus.isRunning())
        return;

    // handle enumeration
    if (this->status & JD_CONTROL_SERVICE_STATUS_ENUMERATE)
    {
        if (this->status & JD_CONTROL_SERVICE_STATUS_ENUMERATING)
        {
            this->device->rolling_counter++;

            if (this->device->rolling_counter > 3)
            {
                this->status &= ~JD_CONTROL_SERVICE_STATUS_ENUMERATING;
                this->status |= JD_CONTROL_SERVICE_STATUS_ENUMERATED;
                this->device->device_flags &= ~JD_DEVICE_FLAGS_PROPOSING;
                this->setConnectionState(true, this->device);
            }
        }
        else
        {
            if (JDControlLayer::instance->bus.isConnected() == false)
            {
                this->device->rolling_counter++;

                if (this->device->rolling_counter > 3)
                {
                    this->setConnectionState(false, this->device);
                    return;
                }
            }
            else
                this->device->rolling_counter = 0;
        }
    }

    // queue a control packet if we have host services.
    if (this->status & JD_CONTROL_SERVICE_STATUS_ENUMERATE)
    {
        enumerationData->udid = this->device->udid;
        enumerationData->device_address = this->device->device_address;
        enumerationData->device_flags = this->device->device_flags;

        send((uint8_t*)enumerationData, formControlPacket());
    }

    // now check to see if remote devices have timed out.
    JDDevice* head = this->remoteDevices;

    while (head)
    {
        JDDevice* dev = head;
        head = head->next;
        dev->rolling_counter++;

        if (dev->rolling_counter > 3)
        {
            this->removeRemoteDevice(dev);
            this->setConnectionState(true, dev);
            free(dev);
        }
    }
}

int JDControlService::enumerate()
{
    if (this->status & JD_CONTROL_SERVICE_STATUS_ENUMERATE)
        return DEVICE_INVALID_STATE;

    if (enumerationData == NULL)
    {
        this->enumerationData = (JDControlPacket*)malloc(JD_MAX_PACKET_SIZE);
        memset(this->enumerationData, 0, JD_MAX_PACKET_SIZE);
    }

    if (device == NULL)
    {
        this->device = (JDDevice*)malloc(sizeof(JDDevice));

        this->device->udid = generate_eui64(target_get_serial());

        // naiive implementation for now... we can sniff the bus for a little before enumerating to
        // get a good first address in the future.
        this->device->device_address = 1 + random(254);

        // set the device state for the control service.
        this->device->device_flags |= JD_DEVICE_FLAGS_PROPOSING;

        this->device->rolling_counter = 0;
        this->device->next = NULL;
        this->device->name = NULL;
    }

    // copy the name into the control packet (if we have one)
    if (this->deviceName.length())
    {
        this->device->device_flags |= JD_DEVICE_FLAGS_HAS_NAME;
        uint8_t* name = enumerationData->data;
        name[0] = this->deviceName.length();
        memcpy(enumerationData->data + 1, this->deviceName.toCharArray(), name[0]);
    }

    int size = formControlPacket();

    if (size > JD_CONTROL_PACKET_HEADER_SIZE)
    {
        this->status |= (JD_CONTROL_SERVICE_STATUS_ENUMERATING | JD_CONTROL_SERVICE_STATUS_ENUMERATE);
        return DEVICE_OK;
    }

    // free enumerationData if we aren't enumerating (no host services)
    free(this->enumerationData);
    this->enumerationData = NULL;

    free(this->device);
    this->device = NULL;

    return DEVICE_INVALID_STATE;
}

/**
 *
 **/
bool JDControlService::isEnumerated()
{
    return this->status & JD_CONTROL_SERVICE_STATUS_ENUMERATED;
}

/**
 *
 **/
bool JDControlService::isEnumerating()
{
    return this->status & JD_CONTROL_SERVICE_STATUS_ENUMERATING;
}

int JDControlService::disconnect()
{
    if (!(this->status & JD_CONTROL_SERVICE_STATUS_ENUMERATE))
        return DEVICE_INVALID_STATE;

    this->status &= ~JD_CONTROL_SERVICE_STATUS_ENUMERATE;

    return DEVICE_OK;
}

JDControlService::JDControlService(ManagedString deviceName) : JDService(JD_SERVICE_CLASS_CONTROL, HostService), deviceName(deviceName)
{
    this->device = NULL;
    this->remoteDevices = NULL;
    this->enumerationData = NULL;

    status = 0;
    status |= DEVICE_COMPONENT_RUNNING;

    if (EventModel::defaultEventBus)
    {
        EventModel::defaultEventBus->listen(this->id, JD_CONTROL_SERVICE_EVT_TIMER_CALLBACK, this, &JDControlService::timerCallback);
        system_timer_event_every(500, this->id, JD_CONTROL_SERVICE_EVT_TIMER_CALLBACK);
    }
}

int JDControlService::send(uint8_t* buf, int len)
{
    if (JDControlLayer::instance)
        return JDControlLayer::instance->bus.send(buf, len, 0, 0, JDBaudRate::Baud1M);

    return DEVICE_NO_RESOURCES;
}

int JDControlService::handleServiceInformation(JDDevice* device, JDServiceInformation* p)
{
    // nop for now... could be useful in the future for controlling the mode of the logic service?
    return DEVICE_OK;
}

/**
  * Given a control packet, finds the associated service, or if no associated device, associates a remote device with a service.
  **/
int JDControlService::handlePacket(JDPacket* pkt)
{
    JDControlPacket *cp = (JDControlPacket *)pkt->data;

    // address collision check
    if (device->device_address == cp->device_address && this->status & (JD_CONTROL_SERVICE_STATUS_ENUMERATING | JD_CONTROL_SERVICE_STATUS_ENUMERATED))
    {
        // a different device is using our address!!
        if (device->udid != cp->udid)
        {
            // if the device is proposing, we can reject (as per the spec)
            if (cp->device_flags & JD_DEVICE_FLAGS_PROPOSING)
            {
                // if we're proposing too, the remote device has won the address
                if (this->device->device_flags & JD_DEVICE_FLAGS_PROPOSING)
                {
                    this->device->rolling_counter = 0;
                    this->device->device_address = 1 + target_random(254);
                }
                // if our address is established, reject the proposal
                else
                {
                    JDControlPacket rejectCP;

                    rejectCP.device_address = cp->device_address;
                    rejectCP.udid = cp->udid;
                    rejectCP.device_flags = cp->device_flags | JD_DEVICE_FLAGS_REJECT;
                    send((uint8_t *)&rejectCP, JD_CONTROL_PACKET_HEADER_SIZE);
                    JD_DMESG("ASK OTHER TO REASSIGN");
                }

                return DEVICE_OK; // no further processing required.
            }
        }
        // someone has flagged a conflict with our device address, re-enumerate
        else if (cp->device_flags & JD_DEVICE_FLAGS_REJECT)
        {
            this->device->rolling_counter = 0;
            this->device->device_address = 1 + target_random(254);
            return DEVICE_OK;
        }
    }

    // the device has not got a confirmed address (enumerating)... if there was a collision it would be handled above
    if (cp->device_flags & JD_DEVICE_FLAGS_PROPOSING)
        return DEVICE_OK;

    // if a service is relying on a remote device, the control service is maintaining the state.
    JDDevice* remoteDevice = this->getRemoteDevice(cp->device_address, cp->udid);

    if (remoteDevice)
        remoteDevice->rolling_counter = 0; // by resetting the counter to zero we mark the device as "seen".

    // if here, address validation has completed successfully... process service information
    uint8_t* dataPointer = cp->data;
    int service_number = 0;

    // skip name:
    // the size of the name is the first byte of the data payload (if present)
    if (cp->device_flags & JD_DEVICE_FLAGS_HAS_NAME)
        dataPointer += *dataPointer;

    while (dataPointer < cp->data + (pkt->size - JD_CONTROL_PACKET_HEADER_SIZE))
    {
        JDServiceInformation* serviceInfo = (JDServiceInformation *)dataPointer;
        bool handled = false;

        for (int i = 0; i < JD_PROTOCOL_SERVICE_ARRAY_SIZE; i++)
        {
            JDService* current = JDControlLayer::instance->services[i];

            if (current == NULL)
                continue;

            bool class_check = current->service_class == serviceInfo->service_class;

            // if the service is running, route the packet.
            if (current->status & JD_SERVICE_STATUS_FLAGS_INITIALISED)
            {
                bool address_check = current->device->device_address == cp->device_address && current->service_number == service_number;
                bool serial_check = cp->udid == current->device->udid;

                // this boolean is used to override stringent address checks (not needed for broadcast services as they receive all packets) to prevent code duplication
                bool broadcast_override = current->mode == BroadcastHostService;
                JD_DMESG("d a %d, s %d, c %d, i %d, t %c%c%c", current->state.device_address, current->state.udid, current->state.service_class, current->state.flags & JD_SERVICE_STATE_FLAGS_INITIALISED ? 1 : 0, current->state.flags & JD_SERVICE_STATE_FLAGS_BROADCAST ? 'B' : ' ', current->state.flags & JD_SERVICE_STATE_FLAGS_HOST ? 'L' : ' ', current->state.flags & JD_SERVICE_STATE_FLAGS_CLIENT ? 'R' : ' ');

                // check if applicable
                if ((address_check && serial_check && class_check) || (class_check && broadcast_override))
                {
                    // we are receiving a packet from a remote device for a service in broadcast mode.
                    if (broadcast_override && cp->device_address != this->device->device_address)
                    {
                        // create a device representation if none exists
                        if (!remoteDevice)
                            remoteDevice = this->addRemoteDevice(cp, pkt->communication_rate);

                        int idx = service_number / 2;

                        // set the service map for this broadcast service.
                        if (idx % 2 == 0)
                            remoteDevice->broadcast_servicemap[idx] = (remoteDevice->broadcast_servicemap[idx] & 0xF0) | current->service_number;
                        else
                            remoteDevice->broadcast_servicemap[idx] =  current->service_number << 4 | (remoteDevice->broadcast_servicemap[idx] & 0x0F);
                    }

                    // if the service has handled the packet it will return DEVICE_OK.
                    // any non zero return value will cause packet routing to continue
                    if (current->handleServiceInformation(remoteDevice, serviceInfo) == DEVICE_OK)
                    {
                        JD_DMESG("S ABSORBED %d", current->state.device_address);
                        handled = true;
                        break;
                    }
                }
            }
            else if (class_check && current->mode == ClientService)
            {
                JD_DMESG("ITER a %d, s %d, c %d, t %c%c%c", current->state.device_address, current->state.udid, current->state.service_class, current->state.flags & JD_SERVICE_STATE_FLAGS_BROADCAST ? 'B' : ' ', current->state.flags & JD_SERVICE_STATE_FLAGS_HOST ? 'L' : ' ', current->state.flags & JD_SERVICE_STATE_FLAGS_CLIENT ? 'R' : ' ');

                // this service instance is looking for a specific device (either a udid or name)
                if (current->requiredDevice)
                {
                    if ((current->requiredDevice->udid > 0 && current->requiredDevice->udid != cp->udid))
                        continue;

                    // this service is looking for a device with a name, further check required.
                    if (current->requiredDevice->device_flags & JD_DEVICE_FLAGS_HAS_NAME)
                    {
                        if (!(cp->device_flags & JD_DEVICE_FLAGS_HAS_NAME))
                            continue;

                        // the size of the name is the first byte of the data payload (if present)
                        uint8_t len = *cp->data;

                        if (ManagedString((char *)cp->data + 1, len) == ManagedString((char *)current->requiredDevice->name))
                            continue;
                    }
                }

                JD_DMESG("FOUND NEW: %d %d %d", current->state.device_address, current->state.service_class);
                remoteDevice = this->addRemoteDevice(cp, pkt->communication_rate);

                if (current->handleServiceInformation(remoteDevice, (JDServiceInformation*)dataPointer) == DEVICE_OK)
                {
                    current->device = remoteDevice;
                    current->service_number = service_number;
                    current->hostConnected();
                    Event(this->id, JD_CONTROL_SERVICE_EVT_CHANGED);
                    break;
                }
            }
        }

        service_number++;
        dataPointer += JD_SERVICE_INFO_HEADER_SIZE + serviceInfo->advertisement_size;
    }

    return DEVICE_OK;
}