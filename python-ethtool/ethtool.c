#include <Python.h>

#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/types.h>

typedef unsigned long long u64;
typedef __uint32_t u32;
typedef __uint16_t u16;
typedef __uint8_t u8;

#include "ethtool-copy.h"
#include <linux/sockios.h> /* for SIOCETHTOOL */

#define _PATH_PROCNET_DEV "/proc/net/dev"

static PyObject *get_active_devices(PyObject *self __unused, PyObject *args __unused)
{
	PyObject *list;
	int numreqs = 30;
	struct ifconf ifc;
	struct ifreq *ifr;
	int n;

	/* SIOCGIFCONF currently seems to only work properly on AF_INET sockets
	   (as of 2.1.128) */
	/* Open control socket. */
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0) {
		PyErr_SetString(PyExc_OSError, strerror(errno));
		return NULL;
	}

	ifc.ifc_buf = NULL;
	for (;;) {
		ifc.ifc_len = sizeof(struct ifreq) * numreqs;
		ifc.ifc_buf = realloc(ifc.ifc_buf, ifc.ifc_len);

		if (ioctl(skfd, SIOCGIFCONF, &ifc) < 0) {
			PyErr_SetString(PyExc_OSError, strerror(errno));
			free(ifc.ifc_buf);
			close(skfd);
			return NULL;
		}

		if (ifc.ifc_len == (int)sizeof(struct ifreq) * numreqs) {
			/* assume it overflowed and try again */
			numreqs += 10;
			continue;
		}
		break;
	}

	list = PyList_New(0);
	ifr = ifc.ifc_req;
	for (n = 0; n < ifc.ifc_len; n += sizeof(struct ifreq)) {
		if (!(ioctl(skfd, SIOCGIFFLAGS, ifr) < 0))
			if (ifr->ifr_flags & IFF_UP)
				PyList_Append(list, PyString_FromString(ifr->ifr_name));
			ifr++;
	}

	free(ifc.ifc_buf);
	close(skfd);

	return list;
}

static PyObject *get_devices(PyObject *self __unused, PyObject *args __unused)
{
	char buffer[256];
	char *ret;;
	PyObject *list = PyList_New(0);
	FILE *fd = fopen(_PATH_PROCNET_DEV, "r");

	if (fd == NULL) {
		PyErr_SetString(PyExc_OSError, strerror(errno));
		return NULL;
	}
	/* skip over first two lines */
	ret = fgets(buffer, 256, fd); ret = fgets(buffer, 256, fd);
	while (!feof(fd)) {
		char *name = buffer;
		char *end = buffer;

		if (fgets(buffer, 256, fd) == NULL)
			break;
		/* find colon */
		while (end && *end != ':')
			end++;
		*end = 0; /* terminate where colon was */
		while (*name == ' ')
			name++; /* skip over leading whitespace if any */
		PyList_Append(list, PyString_FromString(name));
	}
	fclose(fd);
	return list;
}

static PyObject *get_hwaddress(PyObject *self __unused, PyObject *args)
{
	struct ethtool_cmd ecmd;
	struct ifreq ifr;
	int fd, err;
	char *devname;
	char hwaddr[20];

	if (!PyArg_ParseTuple(args, "s", &devname))
		return NULL;

	/* Setup our control structures. */
	memset(&ecmd, 0, sizeof(ecmd));
	memset(&ifr, 0, sizeof(ifr));
	strncpy(&ifr.ifr_name[0], devname, IFNAMSIZ);
	ifr.ifr_name[IFNAMSIZ - 1] = 0;

	/* Open control socket. */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		PyErr_SetString(PyExc_OSError, strerror(errno));
		return NULL;
	}

	/* Get current settings. */
	err = ioctl(fd, SIOCGIFHWADDR, &ifr);
	if (err < 0) {
		char buf[2048];
		int eno = errno;

		snprintf(buf, sizeof(buf), "[Errno %d] %s", eno, strerror(eno));
		PyErr_SetString(PyExc_IOError, buf);
		close(fd);
		return NULL;
	}

	close(fd);

	sprintf(hwaddr, "%02x:%02x:%02x:%02x:%02x:%02x",
		(unsigned int)ifr.ifr_hwaddr.sa_data[0] % 256,
		(unsigned int)ifr.ifr_hwaddr.sa_data[1] % 256,
		(unsigned int)ifr.ifr_hwaddr.sa_data[2] % 256,
		(unsigned int)ifr.ifr_hwaddr.sa_data[3] % 256,
		(unsigned int)ifr.ifr_hwaddr.sa_data[4] % 256,
		(unsigned int)ifr.ifr_hwaddr.sa_data[5] % 256);

	return PyString_FromString(hwaddr);
}

static PyObject *get_ipaddress(PyObject *self __unused, PyObject *args)
{
	struct ethtool_cmd ecmd;
	struct ifreq ifr;
	int fd, err;
	char *devname;
	char ipaddr[20];

	if (!PyArg_ParseTuple(args, "s", &devname))
		return NULL;

	/* Setup our control structures. */
	memset(&ecmd, 0, sizeof(ecmd));
	memset(&ifr, 0, sizeof(ifr));
	strncpy(&ifr.ifr_name[0], devname, IFNAMSIZ);
	ifr.ifr_name[IFNAMSIZ - 1] = 0;

	/* Open control socket. */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		PyErr_SetString(PyExc_OSError, strerror(errno));
		return NULL;
	}

	/* Get current settings. */
	err = ioctl(fd, SIOCGIFADDR, &ifr);
	if (err < 0) {
		char buf[2048];
		int eno = errno;
		snprintf(buf, sizeof(buf), "[Errno %d] %s", eno, strerror(eno));
		PyErr_SetString(PyExc_IOError, buf);
		close(fd);
		return NULL;
	}

	close(fd);

	sprintf(ipaddr, "%u.%u.%u.%u",
		(unsigned int)ifr.ifr_addr.sa_data[2] % 256,
		(unsigned int)ifr.ifr_addr.sa_data[3] % 256,
		(unsigned int)ifr.ifr_addr.sa_data[4] % 256,
		(unsigned int)ifr.ifr_addr.sa_data[5] % 256);

	return PyString_FromString(ipaddr);
}

static PyObject *get_netmask (PyObject *self __unused, PyObject *args)
{
	struct ethtool_cmd ecmd;
	struct ifreq ifr;
	int fd, err;
	char *devname;
	char netmask[20];

	if (!PyArg_ParseTuple(args, "s", &devname))
		return NULL;

	/* Setup our control structures. */
	memset(&ecmd, 0, sizeof(ecmd));
	memset(&ifr, 0, sizeof(ifr));
	strncpy(&ifr.ifr_name[0], devname, IFNAMSIZ);
	ifr.ifr_name[IFNAMSIZ - 1] = 0;

	/* Open control socket. */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		PyErr_SetString(PyExc_OSError, strerror(errno));
		return NULL;
	}

	/* Get current settings. */
	err = ioctl(fd, SIOCGIFNETMASK, &ifr);
	if (err < 0) {
		char buf[2048];
		int eno = errno;
		snprintf(buf, sizeof(buf), "[Errno %d] %s", eno, strerror(eno));
		PyErr_SetString(PyExc_IOError, buf);
		close(fd);
		return NULL;
	}

	close(fd);

	sprintf(netmask, "%u.%u.%u.%u",
		(unsigned int)ifr.ifr_netmask.sa_data[2] % 256,
		(unsigned int)ifr.ifr_netmask.sa_data[3] % 256,
		(unsigned int)ifr.ifr_netmask.sa_data[4] % 256,
		(unsigned int)ifr.ifr_netmask.sa_data[5] % 256);

	return PyString_FromString(netmask);
}

static PyObject *get_broadcast(PyObject *self __unused, PyObject *args)
{
	struct ethtool_cmd ecmd;
	struct ifreq ifr;
	int fd, err;
	char *devname;
	char broadcast[20];

	if (!PyArg_ParseTuple(args, "s", &devname))
		return NULL;

	/* Setup our control structures. */
	memset(&ecmd, 0, sizeof(ecmd));
	memset(&ifr, 0, sizeof(ifr));
	strncpy(&ifr.ifr_name[0], devname, IFNAMSIZ);
	ifr.ifr_name[IFNAMSIZ - 1] = 0;

	/* Open control socket. */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		PyErr_SetString(PyExc_OSError, strerror(errno));
		return NULL;
	}

	/* Get current settings. */
	err = ioctl(fd, SIOCGIFBRDADDR, &ifr);
	if (err < 0) {
		char buf[2048];
		int eno = errno;
		snprintf(buf, sizeof(buf), "[Errno %d] %s", eno, strerror(eno));
		PyErr_SetString(PyExc_IOError, buf);
		close(fd);
		return NULL;
	}

	close(fd);

	sprintf(broadcast, "%u.%u.%u.%u",
		(unsigned int)ifr.ifr_broadaddr.sa_data[2] % 256,
		(unsigned int)ifr.ifr_broadaddr.sa_data[3] % 256,
		(unsigned int)ifr.ifr_broadaddr.sa_data[4] % 256,
		(unsigned int)ifr.ifr_broadaddr.sa_data[5] % 256);

	return PyString_FromString(broadcast);
}

static PyObject *get_module(PyObject *self __unused, PyObject *args)
{
	struct ethtool_cmd ecmd;
	struct ifreq ifr;
	int fd, err;
	char buf[2048];
	char *devname;

	if (!PyArg_ParseTuple(args, "s", &devname))
		return NULL;

	/* Setup our control structures. */
	memset(&ecmd, 0, sizeof(ecmd));
	memset(&ifr, 0, sizeof(ifr));
	strncpy(&ifr.ifr_name[0], devname, IFNAMSIZ);
	ifr.ifr_name[IFNAMSIZ - 1] = 0;
	ifr.ifr_data = (caddr_t) &buf;
	ecmd.cmd = ETHTOOL_GDRVINFO;
	memcpy(&buf, &ecmd, sizeof(ecmd));

	/* Open control socket. */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		PyErr_SetString(PyExc_OSError, strerror(errno));
		return NULL;
	}

	/* Get current settings. */
	err = ioctl(fd, SIOCETHTOOL, &ifr);

	if (err < 0) {  /* failed? */
		int eno = errno;
		FILE *file;
		int found = 0;
		char driver[100], dev[100];
		close(fd);

		/* Before bailing, maybe it is a PCMCIA/PC Card? */
		file = fopen("/var/lib/pcmcia/stab", "r");
		if (file == NULL) {
			sprintf(buf, "[Errno %d] %s", eno, strerror(eno));
			PyErr_SetString(PyExc_IOError, buf);
			return NULL;
		}

		while (!feof(file)) {
			if (fgets(buf, 2048, file) == NULL)
				break;
			buf[2047] = '\0';
			if (strncmp(buf, "Socket", 6) != 0) {
				if (sscanf(buf, "%*d\t%*s\t%100s\t%*d\t%100s\n", driver, dev) > 0) {
					driver[99] = '\0';
					dev[99] = '\0';
					if (strcmp(devname, dev) == 0) {
						found = 1;
						break;
					}
				}
			}
		}
		fclose(file);
		if (!found) {
			sprintf(buf, "[Errno %d] %s", eno, strerror(eno));
			PyErr_SetString(PyExc_IOError, buf);
			return NULL;
		} else
			return PyString_FromString(driver);
	}

	close(fd);
	return PyString_FromString(((struct ethtool_drvinfo *)buf)->driver);
}

static PyObject *get_businfo(PyObject *self __unused, PyObject *args)
{
	struct ethtool_cmd ecmd;
	struct ifreq ifr;
	int fd, err;
	char buf[1024];
	char *devname;

	if (!PyArg_ParseTuple(args, "s", &devname))
		return NULL;

	/* Setup our control structures. */
	memset(&ecmd, 0, sizeof(ecmd));
	memset(&ifr, 0, sizeof(ifr));
	strncpy(&ifr.ifr_name[0], devname, IFNAMSIZ);
	ifr.ifr_name[IFNAMSIZ - 1] = 0;
	ifr.ifr_data = (caddr_t) &buf;
	ecmd.cmd = ETHTOOL_GDRVINFO;
	memcpy(&buf, &ecmd, sizeof(ecmd));

	/* Open control socket. */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		PyErr_SetString(PyExc_OSError, strerror(errno));
		return NULL;
	}

	/* Get current settings. */
	err = ioctl(fd, SIOCETHTOOL, &ifr);

	if (err < 0) {  /* failed? */
		int eno = errno;
		close(fd);

		sprintf(buf, "[Errno %d] %s", eno, strerror(eno));
		PyErr_SetString(PyExc_IOError, buf);
		return NULL;
	}

	close(fd);
	return PyString_FromString(((struct ethtool_drvinfo *)buf)->bus_info);
}

static int get_dev_int_value(int cmd, PyObject *args, int *value)
{
	struct ethtool_value eval;
	struct ifreq ifr;
	int fd, err;
	char *devname;

	if (!PyArg_ParseTuple(args, "s", &devname))
		return -1;

	/* Setup our control structures. */
	memset(&ifr, 0, sizeof(ifr));
	strncpy(&ifr.ifr_name[0], devname, IFNAMSIZ);
	ifr.ifr_name[IFNAMSIZ - 1] = 0;
	ifr.ifr_data = (caddr_t)&eval;
	eval.cmd = cmd;

	/* Open control socket. */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		PyErr_SetString(PyExc_OSError, strerror(errno));
		return -1;
	}

	/* Get current settings. */
	err = ioctl(fd, SIOCETHTOOL, &ifr);
	if (err < 0) {
		char buf[2048];
		sprintf(buf, "[Errno %d] %s", errno, strerror(errno));
		PyErr_SetString(PyExc_IOError, buf);
	}

	close(fd);
	*value = eval.data;

	return err;
}

static PyObject *get_tso(PyObject *self __unused, PyObject *args)
{
	int value;

	if (get_dev_int_value(ETHTOOL_GTSO, args, &value) < 0)
		return NULL;

	return Py_BuildValue("b", value);
}

static PyObject *get_ufo(PyObject *self __unused, PyObject *args)
{
	int value;

	if (get_dev_int_value(ETHTOOL_GUFO, args, &value) < 0)
		return NULL;

	return Py_BuildValue("b", value);
}

static PyObject *get_gso(PyObject *self __unused, PyObject *args)
{
	int value;

	if (get_dev_int_value(ETHTOOL_GGSO, args, &value) < 0)
		return NULL;

	return Py_BuildValue("b", value);
}

static PyObject *get_sg(PyObject *self __unused, PyObject *args)
{
	int value;

	if (get_dev_int_value(ETHTOOL_GSG, args, &value) < 0)
		return NULL;

	return Py_BuildValue("b", value);
}

static struct PyMethodDef PyEthModuleMethods[] = {
	{
		.ml_name = "get_module",
		.ml_meth = (PyCFunction)get_module,
		.ml_flags = METH_VARARGS,
	},
	{
		.ml_name = "get_businfo",
		.ml_meth = (PyCFunction)get_businfo,
		.ml_flags = METH_VARARGS,
	},
	{
		.ml_name = "get_hwaddr",
		.ml_meth = (PyCFunction)get_hwaddress,
		.ml_flags = METH_VARARGS,
	},
	{
		.ml_name = "get_ipaddr",
		.ml_meth = (PyCFunction)get_ipaddress,
		.ml_flags = METH_VARARGS,
	},
	{
		.ml_name = "get_netmask",
		.ml_meth = (PyCFunction)get_netmask,
		.ml_flags = METH_VARARGS,
	},
	{
		.ml_name = "get_broadcast",
		.ml_meth = (PyCFunction)get_broadcast,
		.ml_flags = METH_VARARGS,
	},
	{
		.ml_name = "get_devices",
		.ml_meth = (PyCFunction)get_devices,
		.ml_flags = METH_VARARGS,
	},
	{
		.ml_name = "get_active_devices",
		.ml_meth = (PyCFunction)get_active_devices,
		.ml_flags = METH_VARARGS,
	},
	{
		.ml_name = "get_tso",
		.ml_meth = (PyCFunction)get_tso,
		.ml_flags = METH_VARARGS,
	},
	{
		.ml_name = "get_ufo",
		.ml_meth = (PyCFunction)get_ufo,
		.ml_flags = METH_VARARGS,
	},
	{
		.ml_name = "get_gso",
		.ml_meth = (PyCFunction)get_gso,
		.ml_flags = METH_VARARGS,
	},
	{
		.ml_name = "get_sg",
		.ml_meth = (PyCFunction)get_sg,
		.ml_flags = METH_VARARGS,
	},
	{	.ml_name = NULL, },
};

PyMODINIT_FUNC initethtool(void)
{
	Py_InitModule("ethtool", PyEthModuleMethods);
}
