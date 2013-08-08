/**************************************************************************
* KWin - the KDE window manager                                          *
* This file is part of the KDE project.                                  *
*                                                                        *
* Copyright (C) 2013 Antonis Tsiapaliokas <kok3rs@gmail.com>             *
*                                                                        *
* This program is free software; you can redistribute it and/or modify   *
* it under the terms of the GNU General Public License as published by   *
* the Free Software Foundation; either version 2 of the License, or      *
* (at your option) any later version.                                    *
*                                                                        *
* This program is distributed in the hope that it will be useful,        *
* but WITHOUT ANY WARRANTY; without even the implied warranty of         *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
* GNU General Public License for more details.                           *
*                                                                        *
* You should have received a copy of the GNU General Public License      *
* along with this program.  If not, see <http://www.gnu.org/licenses/>.  *
**************************************************************************/

#include "model.h"
#include "effectconfig.h"

#include <KDE/KPluginInfo>
#include <KDE/KService>
#include <KDE/KServiceTypeTrader>
#include <KDE/KSharedConfig>
#include <KDE/KCModuleProxy>

#include <QAbstractItemModel>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QHash>
#include <QVariant>
#include <QList>
#include <QString>
#include <QQmlEngine>
#include <QtQml>
#include <QDebug>

namespace KWin {
namespace Compositing {

EffectModel::EffectModel(QObject *parent)
    : QAbstractListModel(parent) {

    QHash<int, QByteArray> roleNames;
    roleNames[NameRole] = "NameRole";
    roleNames[DescriptionRole] = "DescriptionRole";
    roleNames[AuthorNameRole] = "AuthorNameRole";
    roleNames[AuthorEmailRole] = "AuthorEmailRole";
    roleNames[LicenseRole] = "LicenseRole";
    roleNames[VersionRole] = "VersionRole";
    roleNames[CategoryRole] = "CategoryRole";
    roleNames[ServiceNameRole] = "ServiceNameRole";
    roleNames[EffectStatusRole] = "EffectStatusRole";
    setRoleNames(roleNames);
    loadEffects();
}

int EffectModel::rowCount(const QModelIndex &parent) const {
    return m_effectsList.count();
}

QVariant EffectModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) {
        return QVariant();
    }

    EffectData currentEffect = m_effectsList.at(index.row());
    switch (role) {
        case Qt::DisplayRole:
        case NameRole:
            return m_effectsList.at(index.row()).name;
        case DescriptionRole:
            return m_effectsList.at(index.row()).description;
        case AuthorNameRole:
            return m_effectsList.at(index.row()).authorName;
        case AuthorEmailRole:
            return m_effectsList.at(index.row()).authorEmail;
        case LicenseRole:
            return m_effectsList.at(index.row()).license;
        case VersionRole:
            return m_effectsList.at(index.row()).version;
        case CategoryRole:
            return m_effectsList.at(index.row()).category;
        case ServiceNameRole:
            return m_effectsList.at(index.row()).serviceName;
        case EffectStatusRole:
            return m_effectsList.at(index.row()).effectStatus;
        default:
            return QVariant();
    }
}

void EffectModel::loadEffects() {
    EffectData effect;
    KConfigGroup kwinConfig(KSharedConfig::openConfig("kwinrc"), "Plugins");
    QDBusMessage messageLoadEffect = QDBusMessage::createMethodCall("org.kde.kwin", "/Effects", "org.kde.kwin.Effects", "loadEffect");
    QDBusMessage messageUnloadEffect = QDBusMessage::createMethodCall("org.kde.kwin", "/Effects", "org.kde.kwin.Effects", "unloadEffect");

    beginResetModel();
    KService::List offers = KServiceTypeTrader::self()->query("KWin/Effect");
    for(KService::Ptr service : offers) {
        KPluginInfo plugin(service);
        effect.name = plugin.name();
        effect.description = plugin.comment();
        effect.authorName = plugin.author();
        effect.authorEmail = plugin.email();
        effect.license = plugin.license();
        effect.version = plugin.version();
        effect.category = plugin.category();
        effect.serviceName = serviceName(effect.name);
        effect.effectStatus = kwinConfig.readEntry(effect.serviceName + "Enabled", false);

        effect.effectStatus ? messageLoadEffect << effect.serviceName : messageUnloadEffect << effect.serviceName;

        m_effectsList << effect;
    }
    qSort(m_effectsList.begin(), m_effectsList.end(), [](const EffectData &a, const EffectData &b) {
        return a.category < b.category;
    });

    endResetModel();

    QDBusConnection::sessionBus().registerObject("/Effects", this);
    QDBusConnection::sessionBus().send(messageLoadEffect);
    QDBusConnection::sessionBus().send(messageUnloadEffect);
}

QString EffectModel::serviceName(const QString &effectName) {
    //The effect name is something like "Show Fps" and
    //we want something like "showfps"
    return "kwin4_effect_" + effectName.toLower().remove(" ");
}

QString EffectModel::findImage(const QString &imagePath, int size) {
    const QString relativePath("icons/oxygen/" + QString::number(size) + 'x' + QString::number(size) + '/' + imagePath);
    const QString fullImagePath = QStandardPaths::locate(QStandardPaths::GenericDataLocation, relativePath, QStandardPaths::LocateFile);
    return fullImagePath;
}

void EffectModel::reload() {
    m_effectsList.clear();
    loadEffects();
}

EffectView::EffectView(QWindow *parent)
    : QQuickView(parent)
{
    qmlRegisterType<EffectModel>("org.kde.kwin.kwincompositing", 1, 0, "EffectModel");
    qmlRegisterType<EffectConfig>("org.kde.kwin.kwincompositing", 1, 0, "EffectConfig");

    init();
}

void EffectView::init() {
    QString mainFile = QStandardPaths::locate(QStandardPaths::DataLocation, "qml/main.qml", QStandardPaths::LocateFile);
    setResizeMode(QQuickView::SizeRootObjectToView);
    rootContext()->setContextProperty("engineObject", this);
    setSource(QUrl(mainFile));
}

void EffectView::effectStatus(const QString &effectName, bool status) {
    m_effectStatus[effectName] = status;
}

void EffectView::syncConfig() {
    KConfigGroup kwinConfig(KSharedConfig::openConfig("kwinrc"), "Plugins");
    QHash<QString, bool> effectsChanged;

    for (auto it = m_effectStatus.constBegin(); it != m_effectStatus.constEnd(); it++) {
        QVariant boolToString(it.value());
        QString effectName = it.key().toLower();
        QString effectEntry = effectName.remove(" ");
        kwinConfig.writeEntry("kwin4_effect_" + effectEntry + "Enabled", boolToString.toString());
        effectsChanged["kwin4_effect_" + effectEntry] = boolToString.toBool();
    }
    kwinConfig.sync();
}

}//end namespace Compositing
}//end namespace KWin