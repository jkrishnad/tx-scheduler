//! C functions around rts-alloc, Agave's shared-memory allocator, so the C++
//! scheduler can allocate and free in the same region Agave uses.

use rts_alloc::Allocator;
use std::{fs::File, mem::ManuallyDrop, os::fd::FromRawFd};

/// Returned by `rts_allocate` when there is no memory left.
pub const RTS_NO_MEMORY: u64 = u64::MAX;

/// Joins the allocator in the shared-memory file `fd` and returns a handle,
/// or null if the file isn't an allocator or has no free handle slots.
/// The fd stays owned by the caller.
#[no_mangle]
pub extern "C" fn rts_join(fd: i32) -> *mut Allocator {
    // SAFETY: the caller passes an open fd. ManuallyDrop keeps it open:
    // Allocator::join maps the file and doesn't need the fd afterwards.
    let file = ManuallyDrop::new(unsafe { File::from_raw_fd(fd) });
    match Allocator::join(&file) {
        Ok(allocator) => Box::into_raw(Box::new(allocator)),
        Err(_) => std::ptr::null_mut(),
    }
}

/// Allocates `size` bytes and returns their offset in the region, or
/// `RTS_NO_MEMORY`.
///
/// # Safety
/// `handle` must come from `rts_join` and not be closed.
#[no_mangle]
pub unsafe extern "C" fn rts_allocate(handle: *const Allocator, size: u32) -> u64 {
    let allocator = unsafe { &*handle };
    match allocator.allocate(size) {
        // SAFETY: `ptr` was just allocated by this allocator.
        Some(ptr) => unsafe { allocator.offset(ptr) as u64 },
        None => RTS_NO_MEMORY,
    }
}

/// Frees the block at `offset`, whichever process allocated it.
///
/// # Safety
/// `handle` must come from `rts_join` and not be closed. `offset` must be a
/// live allocation in this region, freed only once.
#[no_mangle]
pub unsafe extern "C" fn rts_free(handle: *const Allocator, offset: u64) {
    let allocator = unsafe { &*handle };
    unsafe { allocator.free_offset(offset as usize) };
}

/// Takes back blocks that other processes freed into this handle's slabs.
///
/// # Safety
/// `handle` must come from `rts_join` and not be closed.
#[no_mangle]
pub unsafe extern "C" fn rts_clean_remote_frees(handle: *const Allocator) {
    let allocator = unsafe { &*handle };
    allocator.clean_remote_frees();
}

/// Releases the handle and its slot. Null is ignored.
///
/// # Safety
/// `handle` must come from `rts_join` and not be used afterwards.
#[no_mangle]
pub unsafe extern "C" fn rts_close(handle: *mut Allocator) {
    if !handle.is_null() {
        drop(unsafe { Box::from_raw(handle) });
    }
}
