=========
dispatch_dashboard
=========

Qpid Dispatch Router Horizon plugin

Manual Installation
-------------------

Copy the contents of this directoty to /opt/stack/dispatch_plugin and setup the plugin::

    cd /opt/stack/dispatch_plugin/
    python setup.py sdist

If needed, create a virtual environment and install Horizon dependencies::

    cd /opt/stack/horizon
    python tools/install_venv.py

If needed, set up your ``local_settings.py`` file::

    cp openstack_dashboard/local/local_settings.py.example openstack_dashboard/local/local_settings.py


Install the dispatch dashboard in your horizon virtual environment::

    ./tools/with_venv.sh pip install ../dispatch-plugin/dist/dispatch-0.0.1.tar.gz

And enable it in Horizon::

    cp ../dispatch-plugin/enabled/_4*.py openstack_dashboard/local/enabled

If needed, compress the files::

     ./tools/with-venv.sh python manage.py compress

Run a server in the virtual environment::

    ./tools/with-venv.sh python manage.py runserver 0.0.0.0:8080

The horizon dashboard will be available in your browser at http://localhost:8080/
