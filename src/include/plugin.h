/*
Copyright (C) 2018 Adrian Michel
http://www.amichel.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#ifdef PLUGIN_EXPORTS
#define PLUGIN_API __declspec(dllexport)
#else
#define PLUGIN_API __declspec(dllimport)
#endif

#include "exceptions.h"
#include <set>

/**
 * \defgroup Plugin Plugin classes and APIs
 * '
 * For several complete examples of plugins, using persistence in the registry
 * or not, see the sample projects
 */
/*@{*/
namespace tradery {

/**
 * plugin info: info (id, name, description) + version
 */
class PluginInfo : public Info {
 private:
  const Version _version;

 public:
  /**
   * Copy constructor
   *
   * @param pluginInfo The source PluginInfo
   *
   * @see Info
   * @see Version
   */
  PluginInfo(const PluginInfo& pluginInfo)
      : _version(pluginInfo.version()), Info(pluginInfo) {}
  /**
   * Constructor - takes a reference to a Info object
   *
   * The version is set by default to the current version of the product
   *
   * @param info   The source info
   *
   * @see Info
   * @see Version
   */
  PluginInfo(const Info& info) : Info(info) {}

  /**
   * Returns the plugin version
   *
   * @return The plugin version
   * @see Version
   */
  const Version& version() const { return _version; }
};

/**
 * Abstract base class for all plugin types and instances.
 *
 * Implemented as a template class to allow different type of plugins.
 * Also, implemented as an abstract class to allow for specific implementations
 * of various plugins
 *
 * A plugin must derive from this class and implement its various virtual
 * methods that will define its behavior. The plugin type is determined by the
 * type passed as template argument.
 *
 * The template argument is the type of the plugin. For example, a data source
 * plugin is defined as:
 *
 * \code
 * typedef Plugin< DataSource > DataSourcePlugin;
 * \endcode
 *
 * To be useful, a plugin must be able to instantiate at least one
 * configuration, which is an object of the type of the plugin. Most plugins
 * will be able to create, edit and delete multiple configurations as well as
 * persist them.
 *
 * For example a DataSource plugin will be able to create instances of
 * DataSource derived classes and pass them as pinters when requested.
 *
 * To traverse the list of available configuration, use first/next.
 */
template <class Type>
class Plugin : public PluginInfo {
 public:
  /**
   * Constructor - takes a reference to an Info object
   *
   * @param info   The info for the current plugin
   */
  Plugin(const Info& info) : PluginInfo(info) {}

  /**
   * Returns a pointer to a Info about the first available configuration
   *
   * @return A smart pointer to an Info object, or 0 if no configurations are
   * available
   * @exception PluginException
   *                   Thrown in case of an error
   * @see Info
   */
  virtual InfoPtr first() const throw(PluginException) = 0;
  /**
   * Returns a pointer to an Info object for the next available configuration
   *
   * @return A smart pointer to an Info object, or 0 if no more configurations
   * are available
   * @exception PluginException
   *                   Thrown in case of an error
   * @see Info
   */
  virtual InfoPtr next() const throw(PluginException) = 0;
  /**
   * Gets an instance of a configuration, usually as a pointer or smart pointer.
   *
   * @param id     The id of the configuration
   *
   * @return The configuration pointer, or 0 if could not get
   * @exception PluginException
   *                   Thrown in case of an error
   * @see UniqueId
   * @see Info
   */
  virtual ManagedPtr<Type> get(const UniqueId& id,
                               const std::vector<std::string>* createStrings =
                                   0) throw(PluginException) = 0;
  /**
   * Indicates the configuration capabilities of the plugin
   *
   * If the plugin is capable of creating new configurations, it will return
   * true
   *
   * @return true if can create new configurations, false otherwise
   */
  virtual bool canCreate() const = 0;
  /**
   * creates a new configuration
   *
   * For this method to be called, the canCreate method must return true,
   * indicating that plug-in creation is supported
   *
   * Internally, a plugin can call a dialog box to set parameters, persist the
   * configuration in the registry or do anything that it needs to, the only
   * requirement being that at the end it returns a pointer to a new
   * configuration or 0 if it didn't or couldn't create it.
   *
   * A plug-in configuration can be passed a string that can contain runtime
   * information such as a list of symbols for a symbolssource or other values
   *
   * @param createString
   *               A configuration defined string passed at creation. The
   * default value is an empty string This string can be any value that the
   * plug-in configuration will know how to interpret and handle.
   *
   * @return The pointer to the new configuration, or 0 if no configuration has
   * been created
   * @exception PluginException
   *                   Thrown in case of an error
   * @see UniqueId
   */
  virtual ManagedPtr<Type> create(
      const std::vector<std::string>* createStrings =
          0) throw(PluginException) = 0;
  /**
   * Determines if a specific configuration can be edited
   *
   * @param id     The id of the configuration that is tested
   *
   * @return true if the configuration can be edited, false otherwise
   * @see UniqueId
   */
  virtual bool canEdit(const UniqueId& id) const = 0;
  /**
   * Edits a configuration
   *
   * For this method to be called, the canEdit method must return true
   *
   * Internally, a plugin can call a dialog box to change the configuration's
   * parameters, persist them in the registry or do anything that it needs to,
   * the only requirement being that at the end it returns a pointer to the
   * edited configuration
   *
   * @param id     The id of the configuration to be edited
   *
   * @return the edited configuration
   * @exception PluginException
   *                   Thrown in case of an internal error
   * @see UniqueId
   */
  virtual ManagedPtr<Type> edit(const UniqueId& id) throw(PluginException) = 0;
  /**
   * Determines if a specific configuration can be removed
   *
   * @param id     The id of the configuration to be removed
   *
   * @return true if it can be removed, false if not
   * @see UniqueId
   */
  virtual bool canRemove(const UniqueId& id) const = 0;
  /**
   * Removes a configuration
   *
   * For this method to be called, the canRemove method must return true
   *
   * If the configuration can't be removed, an exception should be thrown
   * indicating the reason
   *
   * @param id     The id of the configuration to be removed
   *
   * @exception PluginException
   *                   Thrown in case of an error, for example of the
   * configuration can't be removed
   * @see UniqueId
   */
  virtual void remove(const UniqueId& id) throw(PluginException) = 0;

  /**
   * Indicates whether a plug-in configuration corresponding to a id is UI
   * enabled
   *
   * The default implementation returns false. If the plug-in configuration id
   * can create its own UI, this method should be overriden and it should return
   * true for that id. In that case, the plug-in configuration will be requested
   * to provide more information about its UI - see the PluginConfiguration
   * class.
   *
   * @param id     The id of the configuration to be checked for UI
   *
   * @see UniqueId
   * @see PluginConfiguration
   */
  virtual bool hasWindow(const UniqueId& id) const { return false; }
};

////////////////////////////////////
// Plugin APIs
//

class PluginTreeException {
 public:
  std::vector<ManagedPtr<Info> > _info;

 public:
  PluginTreeException() {}
  PluginTreeException(const Info& info) { add(info); }
  PluginTreeException(const std::vector<ManagedPtr<Info> >& info) {
    _info.insert(_info.begin(), info.begin(), info.end());
  }

  void add(const Info& info) { _info.push_back(new Info(info)); }
  void add(const ManagedPtr<Info> infoPtr) { _info.push_back(infoPtr); }

  const std::vector<ManagedPtr<Info> >& info() const { return _info; }
  bool empty() const { return _info.empty(); }
  const std::string message() const {
    std::ostringstream o;
    o << "Duplicate ids, the components with these Ids will be ignored: "
      << std::endl;

    for (unsigned int n = 0; n < _info.size(); n++)
      o << _info[n]->id().toString() << ", name: " << _info[n]->name()
        << std::endl;

    return o.str();
  }
};

class PluginLoadingStatusHandler {
 public:
  virtual ~PluginLoadingStatusHandler() {}

  virtual void event(const std::string& event) = 0;
  virtual void done() = 0;
};

class NullPluginLoadingStatusHandler : public PluginLoadingStatusHandler {
  virtual void event(const std::string& event) {}
  virtual void done() {}
};

class PluginExplorer {
 private:
  virtual void process(const std::string& path,
                       PluginLoadingStatusHandler* loadingStatusHandler,
                       std::vector<InfoPtr>& duplicates) = 0;

  static void initIgnoreModulesSet(std::set<std::string>& ignoreModulesSet);

  static bool ignoreModule(const std::string& _fileName);

 public:
  // searches and loads all plug-ins in several paths
  void explore(const std::vector<std::string>& paths, const std::string& ext,
               bool recursive,
               PluginLoadingStatusHandler*
                   loadingStatusHandler) throw(PluginTreeException) {
    std::vector<ManagedPtr<Info> > duplicates;
    for (size_t n = 0; n < paths.size(); n++)
      explore(paths[n], ext, recursive, loadingStatusHandler, duplicates);

    if (!duplicates.empty()) throw PluginTreeException(duplicates);
  }

  void explore(const std::string& path, const std::string& ext, bool recursive,
               PluginLoadingStatusHandler*
                   loadingStatusHandler) throw(PluginTreeException) {
    std::vector<std::string> paths;
    paths.push_back(path);

    explore(paths, ext, recursive, loadingStatusHandler);
  }

 private:
  // search and loads all plug-ins in one path
  void explore(const std::string& p, const std::string& ext, bool recursive,
               PluginLoadingStatusHandler* loadingStatusHandler,
               std::vector<InfoPtr>& duplicates);
};

}  // namespace tradery

// end Plugin group
/*@}*/
