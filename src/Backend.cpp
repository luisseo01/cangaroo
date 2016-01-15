/*

  Copyright (c) 2015, 2016 Hubert Denkmair <hubert@denkmair.de>

  This file is part of cangaroo.

  cangaroo is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  cangaroo is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with cangaroo.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "Backend.h"

#include <QDateTime>

#include <model/CanTrace.h>
#include <model/MeasurementSetup.h>
#include <model/MeasurementNetwork.h>
#include <model/MeasurementInterface.h>
#include <driver/CanDriver.h>
#include <driver/CanInterface.h>
#include <driver/CanListener.h>
#include <parser/dbc/DbcParser.h>

Backend *Backend::_instance = 0;

Backend::Backend()
  : QObject(0),
    _measurementRunning(false),
    _measurementStartTime(0),
    _setup(this)
{
    qRegisterMetaType<CanMessage>("CanMessage");

    setDefaultSetup();
    _trace = new CanTrace(*this, this, 100);
}

Backend &Backend::instance()
{
    if (!_instance) {
        _instance = new Backend;
    }
    return *_instance;
}

Backend::~Backend()
{
    delete _trace;
}

void Backend::addCanDriver(CanDriver *driver)
{
    driver->setId(_drivers.size());
    _drivers.append(driver);
}

bool Backend::startMeasurement()
{
    logMessage(log_level_info, "Starting measurement");

    _measurementStartTime = currentTimeStamp();

    int i=0;
    foreach (MeasurementNetwork *network, _setup.getNetworks()) {
        i++;
        foreach (MeasurementInterface *mi, network->interfaces()) {

            CanInterface *intf = getInterfaceById(mi->canInterface());
            if (intf) {
                intf->applyConfig(*mi);

                logMessage(log_level_info, QString("Listening on interface: %1").arg(intf->getName()));
                intf->open();

                CanListener *listener = new CanListener(0, *this, *intf);
                listener->startThread();
                _listeners.append(listener);
            }
        }
    }

    _measurementRunning = true;
    emit beginMeasurement();
    return true;
}

bool Backend::stopMeasurement()
{
    foreach (CanListener *listener, _listeners) {
        listener->requestStop();
    }

    foreach (CanListener *listener, _listeners) {
        listener->waitFinish();
        logMessage(log_level_info, QString("Closing interface: %1").arg(getInterfaceName(listener->getInterfaceId())));
        listener->getInterface().close();
    }

    qDeleteAll(_listeners);
    _listeners.clear();

    logMessage(log_level_info, "Measurement stopped");

    _measurementRunning = false;

    emit endMeasurement();
    return true;
}

bool Backend::isMeasurementRunning() const
{
    return _measurementRunning;
}

void Backend::loadDefaultSetup(MeasurementSetup &setup)
{
    setup.clear();
    int i = 1;

    foreach (CanDriver *driver, _drivers) {
        driver->update();
        foreach (CanInterfaceId intf, driver->getInterfaceIds()) {
            MeasurementNetwork *network = setup.createNetwork();
            network->setName(QString().sprintf("Network %d", i++));

            MeasurementInterface *mi = new MeasurementInterface();
            mi->setCanInterface(intf);
            mi->setBitrate(500000);
            network->addInterface(mi);
        }
    }
}

void Backend::setDefaultSetup()
{
    loadDefaultSetup(_setup);
}

MeasurementSetup &Backend::getSetup()
{
    return _setup;
}

void Backend::setSetup(MeasurementSetup &new_setup)
{
    _setup.cloneFrom(new_setup);
}

void Backend::saveCanDump(QString filename)
{
    _trace->saveCanDump(filename);
}

double Backend::currentTimeStamp() const
{
    return ((double)QDateTime::currentMSecsSinceEpoch()) / 1000;
}

CanTrace *Backend::getTrace()
{
    return _trace;
}

void Backend::clearTrace()
{
    _trace->clear();
}

CanDbMessage *Backend::findDbMessage(const CanMessage &msg) const
{
    return _setup.findDbMessage(msg);
}

CanInterfaceIdList Backend::getInterfaceList()
{
    CanInterfaceIdList result;
    foreach (CanDriver *driver, _drivers) {
        foreach (CanInterfaceId id, driver->getInterfaceIds()) {
            result.append(id);
        }
    }
    return result;
}

CanDriver *Backend::getDriverById(CanInterfaceId id)
{
    CanDriver *driver = _drivers.value((id>>8) & 0xFF);
    if (!driver) {
        logMessage(log_level_critical, QString("Unable to get driver for interface id: %1. This should never happen.").arg(QString().number(id)));
    }
    return driver;
}

CanInterface *Backend::getInterfaceById(CanInterfaceId id)
{
    CanDriver *driver = getDriverById(id);
    return driver ? driver->getInterfaceById(id) : 0;
}

QString Backend::getInterfaceName(CanInterfaceId id)
{
    CanInterface *intf = getInterfaceById(id);
    if (intf) {
        return intf->getName();
    } else {
        logMessage(log_level_critical, QString("Trying to get name from unknown interface id: %1. This should never happen.").arg(QString().number(id)));
        return "";
    }
}

QString Backend::getDriverName(CanInterfaceId id)
{
    CanDriver *driver = getDriverById(id);
    return driver ? driver->getName() : "";
}

CanDriver *Backend::getDriverByName(QString driverName)
{
    foreach (CanDriver *driver, _drivers) {
        if (driver->getName()==driverName) {
            return driver;
        }
    }
    return 0;
}

CanInterface *Backend::getInterfaceByDriverAndName(QString driverName, QString deviceName)
{
    CanDriver *driver = getDriverByName(driverName);
    if (driver) {
        return driver->getInterfaceByName(deviceName);
    } else {
        return 0;
    }

}

pCanDb Backend::loadDbc(QString filename)
{
    DbcParser parser;

    QFile *dbc = new QFile(filename);
    pCanDb candb(new CanDb());
    parser.parseFile(dbc, *candb);
    delete dbc;

    return candb;
}

double Backend::getMeasurementStartTime() const
{
    return _measurementStartTime;
}

void Backend::logMessage(const log_level_t level, const QString msg)
{
    emit onLogMessage(QDateTime::currentDateTime(), level, msg);
}
