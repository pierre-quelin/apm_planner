#include <QList>

#include "QGCMAVLinkInspector.h"
#include "UASManager.h"
#include "LinkManager.h"
#include "ui_QGCMAVLinkInspector.h"


QGCMAVLinkInspector::QGCMAVLinkInspector(QWidget *parent) :
    QWidget(parent),
    mp_Ui(new Ui::QGCMAVLinkInspector)
{
    mp_Ui->setupUi(this);

    // Make sure "All" is an option for both the system and components
    mp_Ui->systemComboBox->addItem(tr("All"), 0);
    mp_Ui->componentComboBox->addItem(tr("All"), 0);

    // Store metadata for all MAVLink messages.
    QVector<mavlink_message_info_t> mavlinkMsg = MAVLINK_MESSAGE_INFO;
    for(const auto &typeInfo : mavlinkMsg)
    {
        if(!messageInfo.contains(typeInfo.msgid))
        {
            messageInfo.insert(typeInfo.msgid, typeInfo);
        }
    }

    // Set up the column headers for the message listing
    QStringList header;
    header << tr("Name");
    header << tr("Value");
    header << tr("Type");
    mp_Ui->treeWidget->setHeaderLabels(header);
    mp_Ui->treeWidget->sortByColumn(0,Qt::AscendingOrder);

    // Set up the column headers for the rate listing
    QStringList rateHeader;
    rateHeader << tr("Name");
    rateHeader << tr("#ID");
    rateHeader << tr("Rate");
    mp_Ui->rateTreeWidget->setHeaderLabels(rateHeader);
    mp_Ui->rateTreeWidget->hide();

//    connect(mp_Ui->refreshButton, &QPushButton::clicked, this, &ApmCustomFirmwareConfig::FillDeviceList);
//    connect(mp_px4Updater.data(), QOverload<QString>::of(&PX4FirmwareUploader::statusUpdate), this, &ApmCustomFirmwareConfig::statusUpdate);
    //connect(mp_Ui->rateTreeWidget, QOverload<QTreeWidgetItem*, int>::of(&QTreeWidgetItem::itemChanged), this, &QGCMAVLinkInspector::rateTreeItemChanged);
    //connect(mp_Ui->rateTreeWidget, SIGNAL(itemChanged(QTreeWidgetItem*,int)), this, SLOT(rateTreeItemChanged(QTreeWidgetItem*,int)));

    // Connect the UI
    connect(mp_Ui->systemComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QGCMAVLinkInspector::selectDropDownMenuSystem);
    connect(mp_Ui->componentComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,  &QGCMAVLinkInspector::selectDropDownMenuComponent);
    connect(mp_Ui->clearButton, &QPushButton::clicked, this, &QGCMAVLinkInspector::clearView);

    // Connect external connections
    connect(UASManager::instance(), QOverload<UASInterface*>::of(&UASManager::UASCreated), this, &QGCMAVLinkInspector::addSystem);
    connect(LinkManager::instance(), QOverload<LinkInterface*, mavlink_message_t>::of(&LinkManager::messageReceived), this, &QGCMAVLinkInspector::receiveMessage);

    QList<UASInterface*> uasList = UASManager::instance()->getUASList();
    for(UASInterface *uas: qAsConst(uasList))
    {
        addSystem(uas);
    }

    // Attach the UI's refresh rate to a timer.
    connect(&updateTimer, &QTimer::timeout, this, &QGCMAVLinkInspector::refreshView);
    updateTimer.start(updateInterval);
}

void QGCMAVLinkInspector::addSystem(UASInterface* uas)
{
    if (!uas)
    {
        return;
    }
    // FIXME: This is a hack:
    // the getMavLinkProtocol is not exposed in the interface.
    // therefore we cast the type to UAS which implements the method.
    // Moreover we know that there is only ONE MavLinkProtocol so we can take the first one.
    if (!_protocol)
    {
        UAS* theUAS = dynamic_cast<UAS*>(uas);
        if (theUAS)
        {
            _protocol = theUAS->getMavLinkProtocol();
        }
    }

    // Add UAS to UI
    mp_Ui->systemComboBox->addItem(uas->getUASName(), uas->getUASID());
    // Add a tree for a new UAS
    addUAStoTree(uas->getUASID());
}

void QGCMAVLinkInspector::selectDropDownMenuSystem(int dropdownid)
{
    selectedSystemID = mp_Ui->systemComboBox->itemData(dropdownid).toInt();
    rebuildComponentList();

    if (selectedSystemID != 0 && selectedComponentID != 0) {
        mp_Ui->rateTreeWidget->show();
    } else {
        mp_Ui->rateTreeWidget->hide();
    }
}

void QGCMAVLinkInspector::selectDropDownMenuComponent(int dropdownid)
{
    selectedComponentID = mp_Ui->componentComboBox->itemData(dropdownid).toInt();

    if (selectedSystemID != 0 && selectedComponentID != 0) {
        mp_Ui->rateTreeWidget->show();
    } else {
        mp_Ui->rateTreeWidget->hide();
    }
}

void QGCMAVLinkInspector::rebuildComponentList()
{
    mp_Ui->componentComboBox->clear();
    components.clear();

    mp_Ui->componentComboBox->addItem(tr("All"), 0);

    // Fill
    UASInterface* uas = UASManager::instance()->getUASForId(selectedSystemID);
    if (uas)
    {
        QMap<int, QString> components = uas->getComponents();
        for (QMap<int, QString>::Iterator iter = components.begin(); iter != components.end(); ++iter)
        {
            QString name = iter.value();
            mp_Ui->componentComboBox->addItem(name,iter.key());
        }
    }
}

void QGCMAVLinkInspector::addComponent(int uas, int component, const QString& name)
{
    Q_UNUSED(component)
    Q_UNUSED(name)
    
    if (uas != selectedSystemID) return;

    rebuildComponentList();
}

/**
 * Reset the view. This entails clearing all data structures and resetting data from already-
 * received messages.
 */
void QGCMAVLinkInspector::clearView()
{
    QMultiMap<int, mavlink_message_t* >::iterator ite;
    for(ite=uasMessageStorage.begin(); ite!=uasMessageStorage.end();++ite)
    {
        delete ite.value();
        ite.value() = NULL;
    }
    uasMessageStorage.clear();

    QMap<int, QMultiMap<int, QTreeWidgetItem*>* >::iterator iteMsg;
    for (iteMsg = uasMsgTreeItems.begin(); iteMsg!=uasMsgTreeItems.end();++iteMsg)
    {
        QMultiMap<int, QTreeWidgetItem*>* msgTreeItems = iteMsg.value();

        QList<int> groupKeys = msgTreeItems->uniqueKeys();
        QList<int>::iterator listKeys;
        for (listKeys=groupKeys.begin();listKeys!=groupKeys.end();++listKeys)
        {
            delete msgTreeItems->take(*listKeys);
        }
    }
    uasMsgTreeItems.clear();

    QMap<int, QTreeWidgetItem* >::iterator iteTree;
    for(iteTree=uasTreeWidgetItems.begin(); iteTree!=uasTreeWidgetItems.end();++iteTree)
    {
        delete iteTree.value();
        iteTree.value() = NULL;
    }
    uasTreeWidgetItems.clear();
    
    QMultiMap<int, QMap<int, float>* >::iterator iteHz;
    for (iteHz=uasMessageHz.begin(); iteHz!=uasMessageHz.end();++iteHz)
    {

        iteHz.value()->clear();
        delete iteHz.value();
        iteHz.value() = NULL;
    }
    uasMessageHz.clear();

    QMultiMap<int, QMap<int, unsigned int>* >::iterator iteCount;
    for(iteCount=uasMessageCount.begin(); iteCount!=uasMessageCount.end();++iteCount)
    {
        iteCount.value()->clear();
        delete iteCount.value();
        iteCount.value() = NULL;
    }
    uasMessageCount.clear();

    QMultiMap<int, QMap<int, quint64>* >::iterator iteLast;
    for(iteLast=uasLastMessageUpdate.begin(); iteLast!=uasLastMessageUpdate.end();++iteLast)
    {
        iteLast.value()->clear();
        delete iteLast.value();
        iteLast.value() = NULL;
    }
    uasLastMessageUpdate.clear();

    onboardMessageInterval.clear();

    mp_Ui->treeWidget->clear();
    mp_Ui->rateTreeWidget->clear();

}

void QGCMAVLinkInspector::refreshView()
{
    if (_protocol)
    {
        QString message(QString::number(_protocol->getTotalMessagesReceived(selectedSystemID)));
        mp_Ui->msg_received->setText(message);
        message.clear();
        message.append(QString::number(_protocol->getTotalMessagesLost(selectedSystemID)));
        mp_Ui->msg_lost->setText(message);
    }

    QMultiMap<int, mavlink_message_t* >::const_iterator ite;

    for(ite=uasMessageStorage.constBegin(); ite!=uasMessageStorage.constEnd();++ite)
    {

        mavlink_message_t* msg = ite.value();
        // Ignore NULL values
        if (msg->msgid == 0xFF) continue;

        // Update the message frenquency

        // Get the previous frequency for low-pass filtering
        float msgHz = 0.0f;
        QMultiMap<int, QMap<int, float>* >::const_iterator iteHz = uasMessageHz.find(msg->sysid);
        QMap<int, float>* uasMsgHz = iteHz.value();

        while((iteHz != uasMessageHz.end()) && (iteHz.key() == msg->sysid))
        {
            if(iteHz.value()->contains(msg->msgid))
            {
                uasMsgHz = iteHz.value();
                msgHz = iteHz.value()->value(msg->msgid);
                break;
            }
            ++iteHz;
        }

        // Get the number of message received
        float msgCount = 0;
        QMultiMap<int, QMap<int, unsigned int> * >::const_iterator iter = uasMessageCount.find(msg->sysid);
        QMap<int, unsigned int>* uasMsgCount = iter.value();

        while((iter != uasMessageCount.end()) && (iter.key()==msg->sysid))
        {
            if(iter.value()->contains(msg->msgid))
            {
                msgCount = (float) iter.value()->value(msg->msgid);
                uasMsgCount = iter.value();
                break;
            }
            ++iter;
        }

        // Compute the new low-pass filtered frequency and update the message count
        msgHz = (1.0f-updateHzLowpass)* msgHz + updateHzLowpass*msgCount/((float)updateInterval/1000.0f);
        uasMsgHz->insert(msg->msgid,msgHz);
        uasMsgCount->insert(msg->msgid,(unsigned int) 0);

        // Update the tree view
        QString messageName("%1 (%2 Hz, #%3)");
        messageName = messageName.arg(messageInfo[msg->msgid].name).arg(msgHz, 3, 'f', 1).arg(msg->msgid);

        addUAStoTree(msg->sysid);

        // Look for the tree for the UAS sysid
        QMultiMap<int, QTreeWidgetItem*>* msgTreeItems = uasMsgTreeItems.value(msg->sysid);
        if (!msgTreeItems)
        {
            // The UAS tree has not been created yet, no update
            return;
        }

        // Add the message with msgid to the tree if not done yet
        if(!msgTreeItems->contains(msg->msgid))
        {
            QStringList fields;
            fields << messageName;
            QTreeWidgetItem* widget = new QTreeWidgetItem();
            for (unsigned int i = 0; i < messageInfo[msg->msgid].num_fields; ++i)
            {
                QTreeWidgetItem* field = new QTreeWidgetItem();
                widget->addChild(field);
            }
            msgTreeItems->insert(msg->msgid,widget);
            QList<int> groupKeys = msgTreeItems->uniqueKeys();
            int insertIndex = groupKeys.indexOf(msg->msgid);
            uasTreeWidgetItems.value(msg->sysid)->insertChild(insertIndex,widget);
        }

        // Update the message
        QTreeWidgetItem* message = msgTreeItems->value(msg->msgid);
        if(message)
        {
            message->setFirstColumnSpanned(true);
            message->setData(0, Qt::DisplayRole, QVariant(messageName));
            for (unsigned int i = 0; i < messageInfo[msg->msgid].num_fields; ++i)
            {
                updateField(msg->sysid,msg->msgid, i, message->child(i));
            }
        }
    }

    if (selectedSystemID == 0 || selectedComponentID == 0)
    {
        return;
    }

    for (int i = 0; i < 256; ++i)//mavlink_message_t msg, receivedMessages)
    {
        QString msgname(messageInfo[i].name);

        if (msgname.length() < 3) {
            continue;
        }

        if (!msgname.contains("EMPTY")) {
            continue;
        }

        // Update the tree view
        QString messageName("%1");
        messageName = messageName.arg(msgname);
        if (!rateTreeWidgetItems.contains(i))
        {
            QStringList fields;
            fields << messageName;
            fields << QString("%1").arg(i);
            fields << "OFF / --- Hz";
            QTreeWidgetItem* widget = new QTreeWidgetItem(fields);
            widget->setFlags(widget->flags() | Qt::ItemIsEditable);
            rateTreeWidgetItems.insert(i, widget);
            mp_Ui->rateTreeWidget->addTopLevelItem(widget);
        }

        // Set Hz
        //QTreeWidgetItem* message = rateTreeWidgetItems.value(i);
        //message->setData(0, Qt::DisplayRole, QVariant(messageName));
    }
}

void QGCMAVLinkInspector::addUAStoTree(int sysId)
{
    if(!uasTreeWidgetItems.contains(sysId))
    {
        // Add the UAS to the main tree after it has been created
        UASInterface* uas = UASManager::instance()->getUASForId(sysId);

        if (uas)
        {
            QStringList idstring;
            if (uas->getUASName() == "")
            {
                idstring << tr("UAS ") + QString::number(uas->getUASID());
            }
            else
            {
                idstring << uas->getUASName();
            }
            QTreeWidgetItem* uasWidget = new QTreeWidgetItem(idstring);
            uasWidget->setFirstColumnSpanned(true);
            uasTreeWidgetItems.insert(sysId,uasWidget);
            mp_Ui->treeWidget->addTopLevelItem(uasWidget);
            uasMsgTreeItems.insert(sysId,new QMultiMap<int, QTreeWidgetItem*>());
        }
    }
}

void QGCMAVLinkInspector::receiveMessage(LinkInterface* link,mavlink_message_t message)
{
    Q_UNUSED(link);

    quint64 receiveTime;
    
    if (selectedSystemID != 0 && selectedSystemID != message.sysid) return;
    if (selectedComponentID != 0 && selectedComponentID != message.compid) return;

    // Create dynamically an array to store the messages for each UAS
    if (!uasMessageStorage.contains(message.sysid))
    {
        mavlink_message_t* msg = new mavlink_message_t;
        *msg = message;
        uasMessageStorage.insert(message.sysid,msg);
    }

    bool msgFound = false;
    QMultiMap<int, mavlink_message_t* >::const_iterator iteMsg = uasMessageStorage.find(message.sysid);
    mavlink_message_t* uasMessage = iteMsg.value();
    while((iteMsg != uasMessageStorage.end()) && (iteMsg.key() == message.sysid))
    {
        if (iteMsg.value()->msgid == message.msgid)
        {
            msgFound = true;
            uasMessage = iteMsg.value();
            break;
        }
        ++iteMsg;
    }
    if (!msgFound)
    {
        mavlink_message_t* msgIdMessage = new mavlink_message_t;
        *msgIdMessage = message;
        uasMessageStorage.insert(message.sysid,msgIdMessage);
    }
    else
    {
        *uasMessage = message;
    }

    // Looking if this message has already been received once
    msgFound = false;
    QMultiMap<int, QMap<int, quint64>* >::const_iterator ite = uasLastMessageUpdate.find(message.sysid);
    QMap<int, quint64>* lastMsgUpdate = ite.value();
    while((ite != uasLastMessageUpdate.end()) && (ite.key() == message.sysid))
    {   
        if(ite.value()->contains(message.msgid))
        {
            msgFound = true;

            // Point to the found message
            lastMsgUpdate = ite.value();
            break;
        }
        ++ite;
    }

    receiveTime = QGC::groundTimeMilliseconds();

    // If the message doesn't exist, create a map for the frequency, message count and time of reception
    if(!msgFound)
    {
        // Create a map for the message frequency
        QMap<int, float>* messageHz = new QMap<int,float>;
        messageHz->insert(message.msgid,0.0f);
        uasMessageHz.insert(message.sysid,messageHz);

        // Create a map for the message count
        QMap<int, unsigned int>* messagesCount = new QMap<int, unsigned int>;
        messagesCount->insert(message.msgid,0);
        uasMessageCount.insert(message.sysid,messagesCount);

        // Create a map for the time of reception of the message
        QMap<int, quint64>* lastMessage = new QMap<int, quint64>;
        lastMessage->insert(message.msgid,receiveTime);
        uasLastMessageUpdate.insert(message.sysid,lastMessage);

        // Point to the created message
        lastMsgUpdate = lastMessage;
    }
    else
    {
        // The message has been found/created
        if ((lastMsgUpdate->contains(message.msgid))&&(uasMessageCount.contains(message.sysid)))
        {
            // Looking for and updating the message count
            unsigned int count = 0;
            QMultiMap<int, QMap<int, unsigned int>* >::const_iterator iter = uasMessageCount.find(message.sysid);
            QMap<int, unsigned int> * uasMsgCount = iter.value();
            while((iter != uasMessageCount.end()) && (iter.key() == message.sysid))
            {
                if(iter.value()->contains(message.msgid))
                {
                    uasMsgCount = iter.value();
                    count = uasMsgCount->value(message.msgid,0);
                    uasMsgCount->insert(message.msgid,count+1);
                    break;
                }
                ++iter;
            }
        }
        lastMsgUpdate->insert(message.msgid,receiveTime);
    }

    if (selectedSystemID == 0 || selectedComponentID == 0)
    {
        return;
    }

    switch (message.msgid)
    {
        case MAVLINK_MSG_ID_DATA_STREAM:
            {
                mavlink_data_stream_t stream;
                mavlink_msg_data_stream_decode(&message, &stream);
                onboardMessageInterval.insert(stream.stream_id, stream.message_rate);
            }
            break;

    }
}

void QGCMAVLinkInspector::changeStreamInterval(int msgid, int interval)
{
    Q_UNUSED(msgid)
    Q_UNUSED(interval)
    //REQUEST_DATA_STREAM
    if (selectedSystemID == 0 || selectedComponentID == 0) {
        return;
    }

//    mavlink_request_data_stream_t stream;
//    stream.target_system = selectedSystemID;
//    stream.target_component = selectedComponentID;
//    stream.req_stream_id = msgid;
//    stream.req_message_rate = interval;
//    stream.start_stop = (interval > 0);

//    mavlink_message_t msg;
//    mavlink_msg_request_data_stream_encode(_protocol->getSystemId(), _protocol->getComponentId(), &msg, &stream);

//    _protocol->sendMessage(msg);
}

void QGCMAVLinkInspector::rateTreeItemChanged(QTreeWidgetItem* paramItem, int column)
{
    if (paramItem && column > 0) {

        int key = paramItem->data(1, Qt::DisplayRole).toInt();
        QVariant value = paramItem->data(2, Qt::DisplayRole);
        float interval = 1000 / value.toFloat();

        qDebug() << "Stream " << key << "interval" << interval;

        changeStreamInterval(key, interval);
    }
}

QGCMAVLinkInspector::~QGCMAVLinkInspector()
{
    clearView();
    delete mp_Ui;
}

void QGCMAVLinkInspector::updateField(int sysid, int msgid, int fieldid, QTreeWidgetItem* item)
{
    // Add field tree widget item
    const auto p_messageInfo = messageInfo.find(static_cast<quint32>(msgid));
    if(p_messageInfo == messageInfo.end())
    {
        QLOG_INFO() << "No Mavlink message info for message ID:" << msgid << "Cannot send message!";
        return;
    }

    item->setData(0, Qt::DisplayRole, QVariant(p_messageInfo->fields[fieldid].name));
    
    bool msgFound = false;
    QMultiMap<int, mavlink_message_t* >::const_iterator iteMsg = uasMessageStorage.find(sysid);
    mavlink_message_t* uasMessage = iteMsg.value();
    while((iteMsg != uasMessageStorage.end()) && (iteMsg.key() == sysid))
    {
        if (iteMsg.value()->msgid == msgid)
        {
            msgFound = true;
            uasMessage = iteMsg.value();
            break;
        }
        ++iteMsg;
    }

    if (!msgFound)
    {
        return;
    }

    const char *p_payload = _MAV_PAYLOAD(uasMessage);

    switch (p_messageInfo->fields[fieldid].type)
    {
    case MAVLINK_TYPE_CHAR:
        if (p_messageInfo->fields[fieldid].array_length > 0)
        {
            char* str = (char*)(p_payload + p_messageInfo->fields[fieldid].wire_offset);
            // Enforce null termination
            str[p_messageInfo->fields[fieldid].array_length-1] = '\0';
            QString string(str);
            item->setData(2, Qt::DisplayRole, "char");
            item->setData(1, Qt::DisplayRole, string);
        }
        else
        {
            // Single char
            char b = *((char*)(p_payload + p_messageInfo->fields[fieldid].wire_offset));
            item->setData(2, Qt::DisplayRole, QString("char[%1]").arg(p_messageInfo->fields[fieldid].array_length));
            item->setData(1, Qt::DisplayRole, b);
        }
        break;
    case MAVLINK_TYPE_UINT8_T:
        if (p_messageInfo->fields[fieldid].array_length > 0)
        {
            uint8_t* nums = (uint8_t*)(p_payload + p_messageInfo->fields[fieldid].wire_offset);
            // Enforce null termination
            QString tmp("%1, ");
            QString string;
            for (unsigned int j = 0; j < p_messageInfo->fields[fieldid].array_length; ++j)
            {
                string += tmp.arg(nums[j]);
            }
            item->setData(2, Qt::DisplayRole, QString("uint8_t[%1]").arg(p_messageInfo->fields[fieldid].array_length));
            item->setData(1, Qt::DisplayRole, string);
        }
        else
        {
            // Single value
            uint8_t u = *(p_payload + p_messageInfo->fields[fieldid].wire_offset);
            item->setData(2, Qt::DisplayRole, "uint8_t");
            item->setData(1, Qt::DisplayRole, u);
        }
        break;
    case MAVLINK_TYPE_INT8_T:
        if (p_messageInfo->fields[fieldid].array_length > 0)
        {
            int8_t* nums = (int8_t*)(p_payload + p_messageInfo->fields[fieldid].wire_offset);
            // Enforce null termination
            QString tmp("%1, ");
            QString string;
            for (unsigned int j = 0; j < p_messageInfo->fields[fieldid].array_length; ++j)
            {
                string += tmp.arg(nums[j]);
            }
            item->setData(2, Qt::DisplayRole, QString("int8_t[%1]").arg(p_messageInfo->fields[fieldid].array_length));
            item->setData(1, Qt::DisplayRole, string);
        }
        else
        {
            // Single value
            int8_t n = *((int8_t*)(p_payload + p_messageInfo->fields[fieldid].wire_offset));
            item->setData(2, Qt::DisplayRole, "int8_t");
            item->setData(1, Qt::DisplayRole, n);
        }
        break;
    case MAVLINK_TYPE_UINT16_T:
        if (p_messageInfo->fields[fieldid].array_length > 0)
        {
            uint16_t* nums = (uint16_t*)(p_payload + p_messageInfo->fields[fieldid].wire_offset);
            // Enforce null termination
            QString tmp("%1, ");
            QString string;
            for (unsigned int j = 0; j < p_messageInfo->fields[fieldid].array_length; ++j)
            {
                string += tmp.arg(nums[j]);
            }
            item->setData(2, Qt::DisplayRole, QString("uint16_t[%1]").arg(p_messageInfo->fields[fieldid].array_length));
            item->setData(1, Qt::DisplayRole, string);
        }
        else
        {
            // Single value
            uint16_t n = *((uint16_t*)(p_payload + p_messageInfo->fields[fieldid].wire_offset));
            item->setData(2, Qt::DisplayRole, "uint16_t");
            item->setData(1, Qt::DisplayRole, n);
        }
        break;
    case MAVLINK_TYPE_INT16_T:
        if (p_messageInfo->fields[fieldid].array_length > 0)
        {
            int16_t* nums = (int16_t*)(p_payload + p_messageInfo->fields[fieldid].wire_offset);
            // Enforce null termination
            QString tmp("%1, ");
            QString string;
            for (unsigned int j = 0; j < p_messageInfo->fields[fieldid].array_length; ++j)
            {
                string += tmp.arg(nums[j]);
            }
            item->setData(2, Qt::DisplayRole, QString("int16_t[%1]").arg(p_messageInfo->fields[fieldid].array_length));
            item->setData(1, Qt::DisplayRole, string);
        }
        else
        {
            // Single value
            int16_t n = *((int16_t*)(p_payload + p_messageInfo->fields[fieldid].wire_offset));
            item->setData(2, Qt::DisplayRole, "int16_t");
            item->setData(1, Qt::DisplayRole, n);
        }
        break;
    case MAVLINK_TYPE_UINT32_T:
        if (p_messageInfo->fields[fieldid].array_length > 0)
        {
            uint32_t* nums = (uint32_t*)(p_payload + p_messageInfo->fields[fieldid].wire_offset);
            // Enforce null termination
            QString tmp("%1, ");
            QString string;
            for (unsigned int j = 0; j < p_messageInfo->fields[fieldid].array_length; ++j)
            {
                string += tmp.arg(nums[j]);
            }
            item->setData(2, Qt::DisplayRole, QString("uint32_t[%1]").arg(p_messageInfo->fields[fieldid].array_length));
            item->setData(1, Qt::DisplayRole, string);
        }
        else
        {
            // Single value
            float n = *((uint32_t*)(p_payload + p_messageInfo->fields[fieldid].wire_offset));
            item->setData(2, Qt::DisplayRole, "uint32_t");
            item->setData(1, Qt::DisplayRole, n);
        }
        break;
    case MAVLINK_TYPE_INT32_T:
        if (p_messageInfo->fields[fieldid].array_length > 0)
        {
            int32_t* nums = (int32_t*)(p_payload + p_messageInfo->fields[fieldid].wire_offset);
            // Enforce null termination
            QString tmp("%1, ");
            QString string;
            for (unsigned int j = 0; j < p_messageInfo->fields[fieldid].array_length; ++j)
            {
                string += tmp.arg(nums[j]);
            }
            item->setData(2, Qt::DisplayRole, QString("int32_t[%1]").arg(p_messageInfo->fields[fieldid].array_length));
            item->setData(1, Qt::DisplayRole, string);
        }
        else
        {
            // Single value
            int32_t n = *((int32_t*)(p_payload + p_messageInfo->fields[fieldid].wire_offset));
            item->setData(2, Qt::DisplayRole, "int32_t");
            item->setData(1, Qt::DisplayRole, n);
        }
        break;
    case MAVLINK_TYPE_FLOAT:
        if (p_messageInfo->fields[fieldid].array_length > 0)
        {
            float* nums = (float*)(p_payload + p_messageInfo->fields[fieldid].wire_offset);
            // Enforce null termination
            QString tmp("%1, ");
            QString string;
            for (unsigned int j = 0; j < p_messageInfo->fields[fieldid].array_length; ++j)
            {
               string += tmp.arg(nums[j]);
            }
            item->setData(2, Qt::DisplayRole, QString("float[%1]").arg(p_messageInfo->fields[fieldid].array_length));
            item->setData(1, Qt::DisplayRole, string);
        }
        else
        {
            // Single value
            float f = *((float*)(p_payload + p_messageInfo->fields[fieldid].wire_offset));
            item->setData(2, Qt::DisplayRole, "float");
            item->setData(1, Qt::DisplayRole, f);
        }
        break;
    case MAVLINK_TYPE_DOUBLE:
        if (p_messageInfo->fields[fieldid].array_length > 0)
        {
            double* nums = (double*)(p_payload + p_messageInfo->fields[fieldid].wire_offset);
            // Enforce null termination
            QString tmp("%1, ");
            QString string;
            for (unsigned int j = 0; j < p_messageInfo->fields[fieldid].array_length; ++j)
            {
                string += tmp.arg(nums[j]);
            }
            item->setData(2, Qt::DisplayRole, QString("double[%1]").arg(p_messageInfo->fields[fieldid].array_length));
            item->setData(1, Qt::DisplayRole, string);
        }
        else
        {
            // Single value
            double f = *((double*)(p_payload + p_messageInfo->fields[fieldid].wire_offset));
            item->setData(2, Qt::DisplayRole, "double");
            item->setData(1, Qt::DisplayRole, f);
        }
        break;
    case MAVLINK_TYPE_UINT64_T:
        if (p_messageInfo->fields[fieldid].array_length > 0)
        {
            uint64_t* nums = (uint64_t*)(p_payload + p_messageInfo->fields[fieldid].wire_offset);
            // Enforce null termination
            QString tmp("%1, ");
            QString string;
            for (unsigned int j = 0; j < p_messageInfo->fields[fieldid].array_length; ++j)
            {
                string += tmp.arg(nums[j]);
            }
            item->setData(2, Qt::DisplayRole, QString("uint64_t[%1]").arg(p_messageInfo->fields[fieldid].array_length));
            item->setData(1, Qt::DisplayRole, string);
        }
        else
        {
            // Single value
            uint64_t n = *((uint64_t*)(p_payload + p_messageInfo->fields[fieldid].wire_offset));
            item->setData(2, Qt::DisplayRole, "uint64_t");
            item->setData(1, Qt::DisplayRole, (quint64) n);
        }
        break;
    case MAVLINK_TYPE_INT64_T:
        if (p_messageInfo->fields[fieldid].array_length > 0)
        {
            int64_t* nums = (int64_t*)(p_payload + p_messageInfo->fields[fieldid].wire_offset);
            // Enforce null termination
            QString tmp("%1, ");
            QString string;
            for (unsigned int j = 0; j < p_messageInfo->fields[fieldid].array_length; ++j)
            {
                string += tmp.arg(nums[j]);
            }
            item->setData(2, Qt::DisplayRole, QString("int64_t[%1]").arg(p_messageInfo->fields[fieldid].array_length));
            item->setData(1, Qt::DisplayRole, string);
        }
        else
        {
            // Single value
            int64_t n = *((int64_t*)(p_payload + p_messageInfo->fields[fieldid].wire_offset));
            item->setData(2, Qt::DisplayRole, "int64_t");
            item->setData(1, Qt::DisplayRole, (qint64) n);
        }
        break;
    }
}
