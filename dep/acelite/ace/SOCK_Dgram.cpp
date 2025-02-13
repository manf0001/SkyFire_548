#include "ace/SOCK_Dgram.h"

#include "ace/Log_Category.h"
#include "ace/INET_Addr.h"
#include "ace/ACE.h"
#include "ace/OS_NS_string.h"
#include "ace/OS_Memory.h"
#include "ace/OS_NS_ctype.h"
#include "ace/os_include/net/os_if.h"
#include "ace/Truncate.h"
#if defined (ACE_HAS_ALLOC_HOOKS)
# include "ace/Malloc_Base.h"
#endif /* ACE_HAS_ALLOC_HOOKS */

#if !defined (__ACE_INLINE__)
#  include "ace/SOCK_Dgram.inl"
#endif /* __ACE_INLINE__ */

#if defined (ACE_HAS_IPV6) && defined (ACE_WIN32)
#include /**/ <iphlpapi.h>
#endif

// This is a workaround for platforms with non-standard
// definitions of the ip_mreq structure
#if ! defined (IMR_MULTIADDR)
#define IMR_MULTIADDR imr_multiaddr
#endif /* ! defined (IMR_MULTIADDR) */

ACE_BEGIN_VERSIONED_NAMESPACE_DECL

ACE_ALLOC_HOOK_DEFINE (ACE_SOCK_Dgram)

void
ACE_SOCK_Dgram::dump () const
{
#if defined (ACE_HAS_DUMP)
  ACE_TRACE ("ACE_SOCK_Dgram::dump");
#endif /* ACE_HAS_DUMP */
}

// Allows a client to read from a socket without having to provide a
// buffer to read.  This method determines how much data is in the
// socket, allocates a buffer of this size, reads in the data, and
// returns the number of bytes read.

ssize_t
ACE_SOCK_Dgram::recv (iovec *io_vec,
                      ACE_Addr &addr,
                      int flags,
                      const ACE_Time_Value *timeout) const
{
  ACE_TRACE ("ACE_SOCK_Dgram::recv");
#if defined (FIONREAD)
  if( ACE::handle_read_ready (this->get_handle (), timeout) != 1 )
    {
      return -1;
    }

  sockaddr *saddr = (sockaddr *) addr.get_addr ();
  int addr_len = addr.get_size ();
  int inlen;

  if (ACE_OS::ioctl (this->get_handle (),
                     FIONREAD,
                     &inlen) == -1)
    return -1;
  else if (inlen > 0)
    {
#if defined (ACE_HAS_ALLOC_HOOKS)
      ACE_ALLOCATOR_RETURN (io_vec->iov_base,
                            static_cast<char*>(ACE_Allocator::instance()->malloc(sizeof(char) * inlen)),
                            -1);
#else
      ACE_NEW_RETURN (io_vec->iov_base,
                      char[inlen],
                      -1);
#endif /* ACE_HAS_ALLOC_HOOKS */

      ssize_t rcv_len = ACE_OS::recvfrom (this->get_handle (),
                                          (char *) io_vec->iov_base,
                                          inlen,
                                          flags,
                                          (sockaddr *) saddr,
                                          &addr_len);
      if (rcv_len < 0)
        {
#if defined (ACE_HAS_ALLOC_HOOKS)
          ACE_Allocator::instance()->free(io_vec->iov_base);
#else
          delete [] (char *)io_vec->iov_base;
#endif /* ACE_HAS_ALLOC_HOOKS */
          io_vec->iov_base = 0;
        }
      else
        {
          io_vec->iov_len = ACE_Utils::truncate_cast<u_long> (rcv_len);
          addr.set_size (addr_len);
        }
      return rcv_len;
    }
  else
    return 0;
#else
  ACE_UNUSED_ARG (flags);
  ACE_UNUSED_ARG (addr);
  ACE_UNUSED_ARG (io_vec);
  ACE_UNUSED_ARG (timeout);
  ACE_NOTSUP_RETURN (-1);
#endif /* FIONREAD */
}

// Here's the shared open function.  Note that if we are using the
// PF_INET protocol family and the address of LOCAL == the address of
// the special variable SAP_ANY then we are going to arbitrarily bind
// to a portnumber.

int
ACE_SOCK_Dgram::shared_open (const ACE_Addr &local,
                             int protocol_family,
                             int ipv6_only)
{
  ACE_TRACE ("ACE_SOCK_Dgram::shared_open");
  bool error = false;
#if defined (ACE_HAS_IPV6)
  int setting = !!ipv6_only;
  if (protocol_family == PF_INET6 &&
      -1 == ACE_OS::setsockopt (this->get_handle (),
                                IPPROTO_IPV6,
                                IPV6_V6ONLY,
                                (char *)&setting,
                                sizeof (setting)))
    {
      this->close();
      return -1;
    }
#else
  ACE_UNUSED_ARG (ipv6_only);
#endif /* defined (ACE_HAS_IPV6) */

  if (local == ACE_Addr::sap_any)
    {
      if (protocol_family == PF_INET
#if defined (ACE_HAS_IPV6)
          || protocol_family == PF_INET6
#endif /* ACE_HAS_IPV6 */
          )
        {
          if (ACE::bind_port (this->get_handle (),
                              INADDR_ANY,
                              protocol_family) == -1)
            error = true;
        }
    }
  else if (ACE_OS::bind (this->get_handle (),
                         reinterpret_cast<sockaddr *> (local.get_addr ()),
                         local.get_size ()) == -1)
    error = true;

  if (error)
    this->close ();

  return error ? -1 : 0;
}

int
ACE_SOCK_Dgram::open (const ACE_Addr &local,
                      int protocol_family,
                      int protocol,
                      ACE_Protocol_Info *protocolinfo,
                      ACE_SOCK_GROUP g,
                      u_long flags,
                      int reuse_addr,
                      int ipv6_only)
{
  if (ACE_SOCK::open (SOCK_DGRAM,
                      protocol_family,
                      protocol,
                      protocolinfo,
                      g,
                      flags,
                      reuse_addr) == -1)
    return -1;
  else if (this->shared_open (local,
                              protocol_family,
                              ipv6_only) == -1)
    return -1;
  else
    return 0;
}

// Here's the general-purpose open routine.

int
ACE_SOCK_Dgram::open (const ACE_Addr &local,
                      int protocol_family,
                      int protocol,
                      int reuse_addr,
                      int ipv6_only)
{
  ACE_TRACE ("ACE_SOCK_Dgram::open");

  if (local != ACE_Addr::sap_any)
    protocol_family = local.get_type ();
  else if (protocol_family == PF_UNSPEC)
    {
#if defined (ACE_HAS_IPV6)
      protocol_family = ACE::ipv6_enabled () ? PF_INET6 : PF_INET;
#else
      protocol_family = PF_INET;
#endif /* ACE_HAS_IPV6 */
    }

  if (ACE_SOCK::open (SOCK_DGRAM,
                      protocol_family,
                      protocol,
                      reuse_addr) == -1)
    return -1;
  else
    return this->shared_open (local,
                              protocol_family,
                              ipv6_only);
}

// Here's the general-purpose constructor used by a connectionless
// datagram ``server''...

ACE_SOCK_Dgram::ACE_SOCK_Dgram (const ACE_Addr &local,
                                int protocol_family,
                                int protocol,
                                int reuse_addr,
                                int ipv6_only)
{
  ACE_TRACE ("ACE_SOCK_Dgram::ACE_SOCK_Dgram");

  if (this->open (local,
                  protocol_family,
                  protocol,
                  reuse_addr,
                  ipv6_only) == -1)
    ACELIB_ERROR ((LM_ERROR,
                ACE_TEXT ("%p\n"),
                ACE_TEXT ("ACE_SOCK_Dgram")));
}

ACE_SOCK_Dgram::ACE_SOCK_Dgram (const ACE_Addr &local,
                                int protocol_family,
                                int protocol,
                                ACE_Protocol_Info *protocolinfo,
                                ACE_SOCK_GROUP g,
                                u_long flags,
                                int reuse_addr,
                                int ipv6_only)
{
  ACE_TRACE ("ACE_SOCK_Dgram::ACE_SOCK_Dgram");
  if (this->open (local,
                  protocol_family,
                  protocol,
                  protocolinfo,
                  g,
                  flags,
                  reuse_addr,
                  ipv6_only) == -1)
    ACELIB_ERROR ((LM_ERROR,
                ACE_TEXT ("%p\n"),
                ACE_TEXT ("ACE_SOCK_Dgram")));
}

#if defined (ACE_HAS_MSG)
// Send an iovec of size N to ADDR as a datagram (connectionless
// version).

ssize_t
ACE_SOCK_Dgram::send (const iovec iov[],
                      int n,
                      const ACE_Addr &addr,
                      int flags) const
{
  ACE_TRACE ("ACE_SOCK_Dgram::send");
  msghdr send_msg;

  send_msg.msg_iov = (iovec *) iov;
  send_msg.msg_iovlen = n;
#if defined (ACE_HAS_SOCKADDR_MSG_NAME)
  send_msg.msg_name = (struct sockaddr *) addr.get_addr ();
#else
  send_msg.msg_name = (char *) addr.get_addr ();
#endif /* ACE_HAS_SOCKADDR_MSG_NAME */
  send_msg.msg_namelen = addr.get_size ();

#if defined (ACE_HAS_4_4BSD_SENDMSG_RECVMSG)
  send_msg.msg_control = 0;
  send_msg.msg_controllen = 0;
  send_msg.msg_flags = 0;
#elif !defined ACE_LACKS_SENDMSG
  send_msg.msg_accrights    = 0;
  send_msg.msg_accrightslen = 0;
#endif /* ACE_HAS_4_4BSD_SENDMSG_RECVMSG */

#ifdef ACE_WIN32
  send_msg.msg_control = 0;
  send_msg.msg_controllen = 0;
#endif

  return ACE_OS::sendmsg (this->get_handle (),
                          &send_msg,
                          flags);
}

// Recv an iovec of size N to ADDR as a datagram (connectionless
// version).

ssize_t
ACE_SOCK_Dgram::recv (iovec iov[],
                      int n,
                      ACE_Addr &addr,
                      int flags,
                      ACE_INET_Addr *to_addr) const
{
  ACE_TRACE ("ACE_SOCK_Dgram::recv");
  msghdr recv_msg;

#if defined (ACE_HAS_4_4BSD_SENDMSG_RECVMSG) || defined ACE_WIN32
#define ACE_USE_MSG_CONTROL
  union control_buffer {
    cmsghdr control_msg_header;
#if defined (IP_RECVDSTADDR)
    u_char padding[ACE_CMSG_SPACE (sizeof (in_addr))];
#elif defined (IP_PKTINFO)
    u_char padding[ACE_CMSG_SPACE (sizeof (in_pktinfo))];
#endif
#if defined (ACE_HAS_IPV6)
    u_char padding6[ACE_CMSG_SPACE (sizeof (in6_pktinfo))];
#endif
  } cbuf;
#else
  ACE_UNUSED_ARG (to_addr);
#endif

  recv_msg.msg_iov = (iovec *) iov;
  recv_msg.msg_iovlen = n;
#if defined (ACE_HAS_SOCKADDR_MSG_NAME)
  recv_msg.msg_name = (struct sockaddr *) addr.get_addr ();
#else
  recv_msg.msg_name = (char *) addr.get_addr ();
#endif /* ACE_HAS_SOCKADDR_MSG_NAME */
  recv_msg.msg_namelen = addr.get_size ();

#ifdef ACE_USE_MSG_CONTROL
  recv_msg.msg_control = to_addr ? &cbuf : 0;
  recv_msg.msg_controllen = to_addr ? sizeof (cbuf) : 0;
#elif !defined ACE_LACKS_SENDMSG
  recv_msg.msg_accrights = 0;
  recv_msg.msg_accrightslen = 0;
#endif

  ssize_t status = ACE_OS::recvmsg (this->get_handle (),
                                    &recv_msg,
                                    flags);
  addr.set_size (recv_msg.msg_namelen);
  addr.set_type (((sockaddr_in *) addr.get_addr())->sin_family);

#ifdef ACE_USE_MSG_CONTROL
  if (to_addr) {
    this->get_local_addr (*to_addr);
    if (to_addr->get_type() == AF_INET) {
#if defined (IP_RECVDSTADDR) || defined (IP_PKTINFO)
      for (cmsghdr *ptr = ACE_CMSG_FIRSTHDR (&recv_msg); ptr; ptr = ACE_CMSG_NXTHDR (&recv_msg, ptr)) {
#if defined (IP_RECVDSTADDR)
        if (ptr->cmsg_level == IPPROTO_IP && ptr->cmsg_type == IP_RECVDSTADDR) {
          to_addr->set_address ((const char *) (ACE_CMSG_DATA (ptr)),
                                sizeof (struct in_addr),
                                0);
          break;
        }
#else
        if (ptr->cmsg_level == IPPROTO_IP && ptr->cmsg_type == IP_PKTINFO) {
          to_addr->set_address ((const char *) &(((in_pktinfo *) (ACE_CMSG_DATA (ptr)))->ipi_addr),
                                sizeof (struct in_addr),
                                0);
          break;
        }
#endif
      }
#endif
    }
#if defined (ACE_HAS_IPV6) && defined (IPV6_PKTINFO)
    else if (to_addr->get_type() == AF_INET6) {
      for (cmsghdr *ptr = ACE_CMSG_FIRSTHDR (&recv_msg); ptr; ptr = ACE_CMSG_NXTHDR (&recv_msg, ptr)) {
        if (ptr->cmsg_level == IPPROTO_IPV6 && ptr->cmsg_type == IPV6_PKTINFO) {
          to_addr->set_address ((const char *) &(((in6_pktinfo *)(ACE_CMSG_DATA (ptr)))->ipi6_addr),
                                sizeof (struct in6_addr),
                                0);

          break;
        }
      }
    }
#endif
  }
#endif

  return status;
}

#else /* ACE_HAS_MSG */

// Send an iovec of size N to ADDR as a datagram (connectionless
// version).
ssize_t
ACE_SOCK_Dgram::send (const iovec iov[],
                      int n,
                      const ACE_Addr &addr,
                      int flags) const
{
  ACE_TRACE ("ACE_SOCK_Dgram::send");

  size_t length = 0;
  int i;

  // Determine the total length of all the buffers in <iov>.
  for (i = 0; i < n; i++)
#if ! (defined(ACE_LACKS_IOVEC) || defined(ACE_LINUX))
    // The iov_len is unsigned on Linux and when using the ACE iovec struct. If we go
    // ahead and try the if, it will emit a warning.
    if (iov[i].iov_len < 0)
      return -1;
    else
#endif
      length += iov[i].iov_len;

  char *buf = 0;

#if defined (ACE_HAS_ALLOCA)
  buf = alloca (length);
#else
# ifdef ACE_HAS_ALLOC_HOOKS
  ACE_ALLOCATOR_RETURN (buf, (buf *)
                        ACE_Allocator::instance ()->malloc (length), -1);
# else
  ACE_NEW_RETURN (buf,
                  char[length],
                  -1);
# endif /* ACE_HAS_ALLOC_HOOKS */
#endif /* !defined (ACE_HAS_ALLOCA) */

  char *ptr = buf;

  for (i = 0; i < n; i++)
    {
      ACE_OS::memcpy (ptr, iov[i].iov_base, iov[i].iov_len);
      ptr += iov[i].iov_len;
    }

  ssize_t result = ACE_SOCK_Dgram::send (buf, length, addr, flags);
#if !defined (ACE_HAS_ALLOCA)
# ifdef ACE_HAS_ALLOC_HOOKS
  ACE_Allocator::instance ()->free (buf);
# else
  delete [] buf;
# endif /* ACE_HAS_ALLOC_HOOKS */
#endif /* !defined (ACE_HAS_ALLOCA) */
  return result;
}

// Recv an iovec of size N to ADDR as a datagram (connectionless
// version).

ssize_t
ACE_SOCK_Dgram::recv (iovec iov[],
                      int n,
                      ACE_Addr &addr,
                      int flags,
                      ACE_INET_Addr *to_addr) const
{
  ACE_TRACE ("ACE_SOCK_Dgram::recv");

  ssize_t length = 0;
  int i;

  ACE_UNUSED_ARG (to_addr);

  for (i = 0; i < n; i++)
#if ! (defined(ACE_LACKS_IOVEC) || defined(ACE_LINUX))
    // The iov_len is unsigned on Linux and when using the ACE iovec struct. If we go
    // ahead and try the if, it will emit a warning.
    if (iov[i].iov_len < 0)
      return -1;
    else
#endif
      length += iov[i].iov_len;

  char *buf = 0;

#if defined (ACE_HAS_ALLOCA)
  buf = alloca (length);
#else
# ifdef ACE_HAS_ALLOC_HOOKS
  ACE_ALLOCATOR_RETURN (buf, (buf *)
                        ACE_Allocator::instance ()->malloc (length), -1);
# else
  ACE_NEW_RETURN (buf,
                  char[length],
                  -1);
# endif /* ACE_HAS_ALLOC_HOOKS */
#endif /* !defined (ACE_HAS_ALLOCA) */

  length = ACE_SOCK_Dgram::recv (buf, length, addr, flags);

  if (length != -1)
    {
      char *ptr = buf;
      int copyn = length;

      for (i = 0;
           i < n && copyn > 0;
           i++)
        {
          ACE_OS::memcpy (iov[i].iov_base, ptr,
                          // iov_len is int on some platforms, size_t on others
                          copyn > (int) iov[i].iov_len
                            ? (size_t) iov[i].iov_len
                            : (size_t) copyn);
          ptr += iov[i].iov_len;
          copyn -= iov[i].iov_len;
        }
    }

#if !defined (ACE_HAS_ALLOCA)
# ifdef ACE_HAS_ALLOC_HOOKS
  ACE_Allocator::instance ()->free (buf);
# else
  delete [] buf;
# endif /* ACE_HAS_ALLOC_HOOKS */
#endif /* !defined (ACE_HAS_ALLOCA) */
  return length;
}

#endif /* ACE_HAS_MSG */

ssize_t
ACE_SOCK_Dgram::recv (void *buf,
                      size_t n,
                      ACE_Addr &addr,
                      int flags,
                      const ACE_Time_Value *timeout) const
{
  if( ACE::handle_read_ready (this->get_handle (), timeout) == 1 )
    {
      // Goes fine, call <recv> to get data
      return this->recv (buf, n, addr, flags);
    }
  else
    {
      return -1;
    }
}

ssize_t
ACE_SOCK_Dgram::send (const void *buf,
                      size_t n,
                      const ACE_Addr &addr,
                      int flags,
                      const ACE_Time_Value *timeout) const
{
  // Check the status of the current socket.
  if( ACE::handle_write_ready (this->get_handle (), timeout) == 1 )
    {
      // Goes fine, call <send> to transmit the data.
      return this->send (buf, n, addr, flags);
    }
  else
    {
      return -1;
    }
}

#if defined (ACE_HAS_IPV6)
// XXX: This will not work on any operating systems that do not support
//      if_nametoindex or that is not Win32 >= Windows XP/Server 2003
int
ACE_SOCK_Dgram::make_multicast_ifaddr6 (ipv6_mreq *ret_mreq,
                                        const ACE_INET_Addr &mcast_addr,
                                        const ACE_TCHAR *net_if)
{
  ACE_TRACE ("ACE_SOCK_Dgram::make_multicast_ifaddr6");
  ipv6_mreq  lmreq;       // Scratch copy.

  ACE_OS::memset (&lmreq,
                  0,
                  sizeof (lmreq));

#if defined (ACE_WIN32) || !defined (ACE_LACKS_IF_NAMETOINDEX)
  if (net_if != 0)
    {
#if defined (ACE_WIN32)
      int if_ix = 0;
      bool const num_if =
        ACE_OS::ace_isdigit (net_if[0]) &&
        (if_ix = ACE_OS::atoi (net_if)) > 0;

      ULONG bufLen = 15000; // Initial size as per Microsoft
      char *buf = 0;
      ACE_NEW_RETURN (buf, char[bufLen], -1);
      DWORD dwRetVal = 0;
      ULONG iterations = 0;
      ULONG const maxTries = 3;
      PIP_ADAPTER_ADDRESSES pAddrs;
      do
        {
          pAddrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES> (buf);
          dwRetVal = ::GetAdaptersAddresses (AF_INET6, 0, 0, pAddrs, &bufLen);
          if (dwRetVal == ERROR_BUFFER_OVERFLOW)
            {
              delete[] buf;
              ACE_NEW_RETURN (buf, char[bufLen], -1);
              ++iterations;
            }
          else
            {
              break;
            }
        } while (dwRetVal == ERROR_BUFFER_OVERFLOW && iterations < maxTries);

      if (dwRetVal != NO_ERROR)
        {
          delete[] buf;
          errno = EINVAL;
          return -1;
        }

      while (pAddrs)
        {
          if ((num_if && pAddrs->Ipv6IfIndex == static_cast<unsigned int>(if_ix))
              || (!num_if &&
                  (ACE_OS::strcmp (ACE_TEXT_ALWAYS_CHAR (net_if), pAddrs->AdapterName) == 0
                   || ACE_OS::strcmp (ACE_TEXT_ALWAYS_WCHAR (net_if), pAddrs->FriendlyName) == 0)))
            {
              lmreq.ipv6mr_interface = pAddrs->Ipv6IfIndex;
              break;
            }

          pAddrs = pAddrs->Next;
        }

      delete[] buf; // clean up

#else /* ACE_WIN32 */
#ifndef ACE_LACKS_IF_NAMETOINDEX
      lmreq.ipv6mr_interface = ACE_OS::if_nametoindex (ACE_TEXT_ALWAYS_CHAR (net_if));
#endif /* ACE_LACKS_IF_NAMETOINDEX */
#endif /* ACE_WIN32 */
      if (lmreq.ipv6mr_interface == 0)
        {
          errno = EINVAL;
          return -1;
        }
    }
#else  /* ACE_WIN32 || !ACE_LACKS_IF_NAMETOINDEX */
    ACE_UNUSED_ARG(net_if);
#endif /* ACE_WIN32 || !ACE_LACKS_IF_NAMETOINDEX */

  // now set the multicast address
  ACE_OS::memcpy (&lmreq.ipv6mr_multiaddr,
                  &((sockaddr_in6 *) mcast_addr.get_addr ())->sin6_addr,
                  sizeof (in6_addr));

  // Set return info, if requested.
  if (ret_mreq)
    *ret_mreq = lmreq;

  return 0;
}
#endif /* ACE_HAS_IPV6 */

ACE_END_VERSIONED_NAMESPACE_DECL
