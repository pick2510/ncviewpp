// Copyright (C) 2026 Dominik Strebel
//
// Characterization tests for core/src/util.cc's handle_dim_mapping()/
// handle_dim_mapping_2d()/determine_lat_lon() -- the "coordinates
// attribute" mapping-dim machinery (2-D curvilinear coordinates, e.g. WRF's
// XLAT/XLONG), which had zero direct tests before this file. Written
// before Phase 4b ("refine the architecture" plan) moves these functions
// out of util.cc.
//
// Correction to the plan's own inventory (found while writing this file,
// per this plan's "verify before acting" discipline): determine_lat_lon()
// does NOT sniff units strings. It classifies a coordinate variable's NAME
// (via strncasecmp/strstr against "lat"/"lon", falling back to a bare
// 'x'/'y' first letter) -- the plan's "unit-string sniffing" description
// was wrong. Tests below exercise the actual, name-based behavior.
//
// dataset.h's own header comment records a deliberate prior design
// decision: fill_dim_structs()/handle_dim_mapping()/is_scannable() "stay
// free functions ... called from Dataset's methods the same way anything
// else calls them" rather than becoming Dataset methods, because they only
// fill in an already-allocated NCVar*'s fields and don't touch Dataset's
// variable list itself. Phase 4b's move respects that decision (these move
// to var_metadata.cc as free functions, not Dataset methods) rather than
// the plan's original "private Dataset methods" destination, which this
// file's own reading of dataset.h contradicts.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "test_udunits_helper.h"

namespace {

// Writes a file with a 2-D data var (y,x) and a same-shaped 2-D coordinate
// variable named coord_var_name, referenced via a "coordinates" attribute
// on the data var (the CF convention handle_dim_mapping() looks for).
std::string make_2d_coord_piece(const char *data_var_name, const char *coord_var_name,
                                 int ny, int nx) {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_dimmap_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_y, dim_x;
    REQUIRE(nc_def_dim(ncid, "y", ny, &dim_y) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "x", nx, &dim_x) == NC_NOERR);

    int dims[2] = {dim_y, dim_x};
    int var_coord, var_data;
    REQUIRE(nc_def_var(ncid, coord_var_name, NC_FLOAT, 2, dims, &var_coord) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, data_var_name, NC_FLOAT, 2, dims, &var_data) == NC_NOERR);
    std::string coords_att = coord_var_name;
    REQUIRE(nc_put_att_text(ncid, var_data, "coordinates", coords_att.size(), coords_att.c_str()) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);

    std::vector<float> coord_vals((size_t)ny * nx), data_vals((size_t)ny * nx);
    for (int i = 0; i < ny; i++)
        for (int j = 0; j < nx; j++) {
            coord_vals[(size_t)i * nx + j] = (float)(i * nx + j);
            data_vals[(size_t)i * nx + j] = (float)(i * 10 + j);
        }
    REQUIRE(nc_put_var_float(ncid, var_coord, coord_vals.data()) == NC_NOERR);
    REQUIRE(nc_put_var_float(ncid, var_data, data_vals.data()) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);
    return path;
}

// A file with two 2-D coordinate vars (lat-like and lon-like) mapping one
// data var, the realistic WRF-style case handle_dim_mapping_2d()'s n_matches==2
// branch handles.
std::string make_2d_latlon_piece(const char *data_var_name,
                                  const char *lat_name, const char *lon_name,
                                  int ny, int nx) {
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_dimmap_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;

    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_y, dim_x;
    REQUIRE(nc_def_dim(ncid, "y", ny, &dim_y) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "x", nx, &dim_x) == NC_NOERR);
    int dims[2] = {dim_y, dim_x};

    int var_lat, var_lon, var_data;
    REQUIRE(nc_def_var(ncid, lat_name, NC_FLOAT, 2, dims, &var_lat) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, lon_name, NC_FLOAT, 2, dims, &var_lon) == NC_NOERR);
    REQUIRE(nc_def_var(ncid, data_var_name, NC_FLOAT, 2, dims, &var_data) == NC_NOERR);
    std::string coords_att = std::string(lat_name) + " " + lon_name;
    REQUIRE(nc_put_att_text(ncid, var_data, "coordinates", coords_att.size(), coords_att.c_str()) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);

    std::vector<float> lat_vals((size_t)ny * nx), lon_vals((size_t)ny * nx), data_vals((size_t)ny * nx);
    for (int i = 0; i < ny; i++)
        for (int j = 0; j < nx; j++) {
            lat_vals[(size_t)i * nx + j] = 10.0f + i;
            lon_vals[(size_t)i * nx + j] = 100.0f + j;
            data_vals[(size_t)i * nx + j] = (float)(i * 10 + j);
        }
    REQUIRE(nc_put_var_float(ncid, var_lat, lat_vals.data()) == NC_NOERR);
    REQUIRE(nc_put_var_float(ncid, var_lon, lon_vals.data()) == NC_NOERR);
    REQUIRE(nc_put_var_float(ncid, var_data, data_vals.data()) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);
    return path;
}

int open_for_core(const std::string &path) {
    Stringlist *files = nullptr;
    stringlist_add_string(&files, path.c_str());
    determine_file_type(files);
    stringlist_delete_entire_list(files);
    return netcdf_fi_initialize(const_cast<char *>(path.c_str()));
}

} // namespace

TEST_CASE("handle_dim_mapping: a single unrecognized-name 2-D coordinate var abandons the mapping") {
    ensure_ncview_misc_initialized();
    std::string path = make_2d_coord_piece("dimmap_unrecognized", "totally_unnamed_thing", 3, 4);
    int fileid = open_for_core(path);
    g_dataset.addVariable("dimmap_unrecognized", fileid, path.c_str());
    NCVar *var = g_dataset.findVariable("dimmap_unrecognized");
    REQUIRE(var != nullptr);

    // determine_lat_lon() can't classify "totally_unnamed_thing" as lat or
    // lon by name -- handle_dim_mapping_2d() aborts and resets every entry.
    for (auto &m : var->dim_map_info)
        CHECK(m == nullptr);

    std::remove(path.c_str());
}

TEST_CASE("handle_dim_mapping: two 2-D coordinate vars (lat-like and lon-like names) map both dims") {
    ensure_ncview_misc_initialized();
    std::string path = make_2d_latlon_piece("dimmap_wrf_style", "XLAT", "XLONG", 3, 4);
    int fileid = open_for_core(path);
    g_dataset.addVariable("dimmap_wrf_style", fileid, path.c_str());
    NCVar *var = g_dataset.findVariable("dimmap_wrf_style");
    REQUIRE(var != nullptr);
    REQUIRE(var->n_dims == 2);

    // Both dims should have a mapping entry: dim 0 (y) mapped by the
    // latitude-like var (matched to the leftmost/first dim per
    // handle_dim_mapping_2d()'s "match lat to the first one on the left"
    // rule), dim 1 (x) mapped by the longitude-like var (matched to the
    // rightmost/last dim).
    REQUIRE(var->dim_map_info[0] != nullptr);
    REQUIRE(var->dim_map_info[1] != nullptr);
    CHECK(var->dim_map_info[0]->coord_var_name == "XLAT");
    CHECK(var->dim_map_info[1]->coord_var_name == "XLONG");
    // handle_dim_mapping() calls handle_dim_mapping_2d() once PER TOKEN in
    // the "coordinates" attribute, so XLAT and XLONG each get their own,
    // separate NCDim_map_info -- not one struct shared across both dims
    // (an earlier version of this test wrongly assumed the latter).
    CHECK(var->dim_map_info[0].get() != var->dim_map_info[1].get());

    std::remove(path.c_str());
}

TEST_CASE("handle_dim_mapping: a var with no coordinates attribute leaves every dim_map_info entry null") {
    ensure_ncview_misc_initialized();
    // Build a plain file with no "coordinates" attribute at all.
    auto tmpl = (std::filesystem::temp_directory_path() / "ncview_dimmap_XXXXXX").string();
    int fd = mkstemp(&tmpl[0]);
    REQUIRE(fd >= 0);
    close(fd);
    std::string path = tmpl;
    int ncid;
    REQUIRE(nc_create(path.c_str(), NC_CLOBBER, &ncid) == NC_NOERR);
    int dim_y, dim_x, var_data;
    REQUIRE(nc_def_dim(ncid, "y", 3, &dim_y) == NC_NOERR);
    REQUIRE(nc_def_dim(ncid, "x", 4, &dim_x) == NC_NOERR);
    int dims[2] = {dim_y, dim_x};
    REQUIRE(nc_def_var(ncid, "dimmap_plain", NC_FLOAT, 2, dims, &var_data) == NC_NOERR);
    REQUIRE(nc_enddef(ncid) == NC_NOERR);
    std::vector<float> vals(12, 0.0f);
    REQUIRE(nc_put_var_float(ncid, var_data, vals.data()) == NC_NOERR);
    REQUIRE(nc_close(ncid) == NC_NOERR);

    int fileid = open_for_core(path);
    g_dataset.addVariable("dimmap_plain", fileid, path.c_str());
    NCVar *var = g_dataset.findVariable("dimmap_plain");
    REQUIRE(var != nullptr);
    for (auto &m : var->dim_map_info)
        CHECK(m == nullptr);

    std::remove(path.c_str());
}

// determine_lat_lon() is a file-static helper (no external linkage), like
// do_print.cc's build_print_info() from Phase 4a -- so its name-based
// classification is exercised indirectly, through handle_dim_mapping_2d(),
// which is the only caller. These cover its remaining branches beyond the
// XLAT/XLONG substring match already covered above: a bare 'y'/'x' first
// letter, and the case-insensitive "lat"/"lon" prefix match.
TEST_CASE("handle_dim_mapping: bare Y/X-prefixed coordinate var names are classified as lat/lon") {
    ensure_ncview_misc_initialized();
    std::string path = make_2d_latlon_piece("dimmap_yx_style", "Ycoord", "Xcoord", 3, 4);
    int fileid = open_for_core(path);
    g_dataset.addVariable("dimmap_yx_style", fileid, path.c_str());
    NCVar *var = g_dataset.findVariable("dimmap_yx_style");
    REQUIRE(var != nullptr);
    REQUIRE(var->dim_map_info[0] != nullptr);
    REQUIRE(var->dim_map_info[1] != nullptr);
    CHECK(var->dim_map_info[0]->coord_var_name == "Ycoord");
    CHECK(var->dim_map_info[1]->coord_var_name == "Xcoord");

    std::remove(path.c_str());
}

TEST_CASE("handle_dim_mapping: case-insensitive Latitude/Longitude prefixed names are classified") {
    ensure_ncview_misc_initialized();
    std::string path = make_2d_latlon_piece("dimmap_latlon_style", "Latitude", "Longitude", 3, 4);
    int fileid = open_for_core(path);
    g_dataset.addVariable("dimmap_latlon_style", fileid, path.c_str());
    NCVar *var = g_dataset.findVariable("dimmap_latlon_style");
    REQUIRE(var != nullptr);
    REQUIRE(var->dim_map_info[0] != nullptr);
    REQUIRE(var->dim_map_info[1] != nullptr);
    CHECK(var->dim_map_info[0]->coord_var_name == "Latitude");
    CHECK(var->dim_map_info[1]->coord_var_name == "Longitude");

    std::remove(path.c_str());
}
