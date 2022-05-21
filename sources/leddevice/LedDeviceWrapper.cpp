#include <leddevice/LedDeviceWrapper.h>

#include <leddevice/LedDevice.h>
#include <leddevice/LedDeviceFactory.h>

// following file is auto generated by cmake! it contains all available leddevice headers
#include "LedDevice_headers.h"

// util
#include <base/HyperHdrInstance.h>
#include <utils/JsonUtils.h>

// qt
#include <QMutexLocker>
#include <QThread>
#include <QDir>

LedDeviceRegistry LedDeviceWrapper::_ledDeviceMap{};


QMutex LedDeviceWrapper::_ledDeviceMapLock;


LedDeviceWrapper::LedDeviceWrapper(HyperHdrInstance* hyperhdr)
	: QObject(hyperhdr)
	, _hyperhdr(hyperhdr)
	, _ledDevice(nullptr)
	, _enabled(false)
{
	// prepare the device constructor map
#define REGISTER(className) LedDeviceWrapper::addToDeviceMap(QString(#className).toLower(), LedDevice##className::construct);

// the REGISTER() calls are auto-generated by cmake.
#include "LedDevice_register.cpp"

#undef REGISTER

	_hyperhdr->setNewComponentState(hyperhdr::COMP_LEDDEVICE, false);
}

LedDeviceWrapper::~LedDeviceWrapper()
{
	stopDeviceThread();
}

void LedDeviceWrapper::createLedDevice(const QJsonObject& config)
{
	if (_ledDevice != nullptr)
	{
		stopDeviceThread();
	}

	// create thread and device
	QThread* thread = new QThread(this);
	thread->setObjectName("LedDeviceThread");
	_ledDevice = LedDeviceFactory::construct(config);
	_ledDevice->moveToThread(thread);

	// setup thread management
	connect(thread, &QThread::started, _ledDevice, &LedDevice::start, Qt::QueuedConnection);

	// further signals
	connect(this, &LedDeviceWrapper::updateLeds, _ledDevice, &LedDevice::updateLeds, Qt::QueuedConnection);

	connect(this, &LedDeviceWrapper::stopLedDevice, _ledDevice, &LedDevice::stop, Qt::BlockingQueuedConnection);

	connect(_ledDevice, &LedDevice::enableStateChanged, this, &LedDeviceWrapper::handleInternalEnableState, Qt::QueuedConnection);

	connect(_ledDevice, &LedDevice::newCounter, this, [=](PerformanceReport pr) {pr.id = this->_hyperhdr->getInstanceIndex(); emit PerformanceCounters::getInstance()->newCounter(pr); });

	// start the thread
	thread->start();
}

void LedDeviceWrapper::handleComponentState(hyperhdr::Components component, bool state)
{
	if (component == hyperhdr::COMP_LEDDEVICE)
	{
		if (state)
		{
			QMetaObject::invokeMethod(_ledDevice, "enable");
		}
		else
		{
			QMetaObject::invokeMethod(_ledDevice, "disable");
		}

		QMetaObject::invokeMethod(_ledDevice, "componentState", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, _enabled));
	}
}

void LedDeviceWrapper::handleInternalEnableState(bool newState)
{
	_hyperhdr->setNewComponentState(hyperhdr::COMP_LEDDEVICE, newState);
	_enabled = newState;

	if (_enabled)
	{
		_hyperhdr->update();
	}
}

void LedDeviceWrapper::stopDeviceThread()
{
	// turns the LEDs off & stop refresh timers
	emit stopLedDevice();

	// get current thread
	QThread* oldThread = _ledDevice->thread();
	disconnect(oldThread, nullptr, nullptr, nullptr);
	oldThread->quit();
	oldThread->wait();
	delete oldThread;

	disconnect(_ledDevice, nullptr, nullptr, nullptr);
	delete _ledDevice;
	_ledDevice = nullptr;
}

QString LedDeviceWrapper::getActiveDeviceType() const
{
	QString value = 0;
	QMetaObject::invokeMethod(_ledDevice, "getActiveDeviceType", Qt::BlockingQueuedConnection, Q_RETURN_ARG(QString, value));
	return value;
}

unsigned int LedDeviceWrapper::getLedCount() const
{
	int value = 0;
	QMetaObject::invokeMethod(_ledDevice, "getLedCount", Qt::BlockingQueuedConnection, Q_RETURN_ARG(int, value));
	return value;
}

bool LedDeviceWrapper::enabled() const
{
	return _enabled;
}

int LedDeviceWrapper::addToDeviceMap(QString name, LedDeviceCreateFuncType funcPtr)
{
	QMutexLocker lock(&_ledDeviceMapLock);

	_ledDeviceMap.emplace(name, funcPtr);

	return 0;
}


const LedDeviceRegistry& LedDeviceWrapper::getDeviceMap()
{
	QMutexLocker lock(&_ledDeviceMapLock);

	return _ledDeviceMap;
}

QJsonObject LedDeviceWrapper::getLedDeviceSchemas()
{
	// make sure the resources are loaded (they may be left out after static linking)
	Q_INIT_RESOURCE(LedDeviceSchemas);

	// read the JSON schema from the resource
	QDir dir(":/leddevices/");
	QJsonObject result, schemaJson;

	for (QString& item : dir.entryList())
	{
		QString schemaPath(QString(":/leddevices/") + item);
		QString devName = item.remove("schema-");

		QString data;
		if (!FileUtils::readFile(schemaPath, data, Logger::getInstance("LedDevice")))
		{
			throw std::runtime_error("ERROR: Schema not found: " + item.toStdString());
		}

		QJsonObject schema;
		if (!JsonUtils::parse(schemaPath, data, schema, Logger::getInstance("LedDevice")))
		{
			throw std::runtime_error("ERROR: JSON schema wrong of file: " + item.toStdString());
		}

		schemaJson = schema;
		schemaJson["title"] = QString("edt_dev_spec_header_title");

		result[devName] = schemaJson;
	}

	return result;
}
