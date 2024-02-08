///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, Enrico Turri @enricoturri1966
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2015 Maksim Derbasov @ntfshard
///|/ Copyright (c) 2014 Petr Ledvina @ledvinap
///|/
///|/ ported from lib/Slic3r/Polyline.pm:
///|/ Copyright (c) Prusa Research 2018 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2014 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2012 Mark Hindess
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "BoundingBox.hpp"
#include "Polyline.hpp"
#include "Exception.hpp"
#include "ExPolygon.hpp"
#include "Line.hpp"
#include "Polygon.hpp"
#include <iostream>
#include <utility>
#include <algorithm>

namespace Slic3r {

const Point& Polyline::leftmost_point() const
{
    const Point *p = &this->points.front();
    for (Points::const_iterator it = this->points.begin() + 1; it != this->points.end(); ++ it) {
        if (it->x() < p->x()) 
        	p = &(*it);
    }
    return *p;
}

double Polyline::length() const
{
    double l = 0;
    for (size_t i = 1; i < this->points.size(); ++ i)
        l += (this->points[i] - this->points[i - 1]).cast<double>().norm();
    return l;
}

Lines Polyline::lines() const
{
    Lines lines;
    if (this->points.size() >= 2) {
        lines.reserve(this->points.size() - 1);
        for (Points::const_iterator it = this->points.begin(); it != this->points.end()-1; ++it) {
            lines.push_back(Line(*it, *(it + 1)));
        }
    }
    return lines;
}

// removes the given distance from the end of the polyline
void Polyline::clip_end(coordf_t distance)
{
    while (distance > 0) {
        Vec2d  last_point = this->last_point().cast<coordf_t>();
        this->points.pop_back();
        if (this->points.empty())
            break;
        Vec2d  v    = this->last_point().cast<coordf_t>() - last_point;
        coordf_t lsqr = v.squaredNorm();
        if (lsqr > distance * distance) {
            this->points.emplace_back((last_point + v * (distance / sqrt(lsqr))).cast<coord_t>());
            return;
        }
        distance -= sqrt(lsqr);
    }
}

// removes the given distance from the start of the polyline
void Polyline::clip_start(coordf_t distance)
{
    this->reverse();
    this->clip_end(distance);
    if (this->points.size() >= 2)
        this->reverse();
}

void Polyline::extend_end(coordf_t distance)
{
    // relocate last point by extending the last segment by the specified length
    Vec2d v = (this->points.back() - *(this->points.end() - 2)).cast<coordf_t>().normalized();
    this->points.back() += (v * distance).cast<coord_t>();
}

void Polyline::extend_start(coordf_t distance)
{
    // relocate first point by extending the first segment by the specified length
    Vec2d v = (this->points.front() - this->points[1]).cast<coordf_t>().normalized();
    this->points.front() += (v * distance).cast<coord_t>();
}

/* this method returns a collection of points picked on the polygon contour
   so that they are evenly spaced according to the input distance */
Points Polyline::equally_spaced_points(coordf_t distance) const
{
    Points points;
    points.emplace_back(this->first_point());
    double len = 0;
    
    for (Points::const_iterator it = this->points.begin() + 1; it != this->points.end(); ++it) {
        Vec2d  p1 = (it-1)->cast<double>();
        Vec2d  v  = it->cast<double>() - p1;
        coordf_t segment_length = v.norm();
        len += segment_length;
        if (len < distance)
            continue;
        if (len == distance) {
            points.emplace_back(*it);
            len = 0;
            continue;
        }
        coordf_t take = segment_length - (len - distance);  // how much we take of this segment
        points.emplace_back((p1 + v * (take / v.norm())).cast<coord_t>());
        -- it;
        len = - take;
    }
    return points;
}

void Polyline::simplify(coordf_t tolerance)
{
    this->points = MultiPoint::douglas_peucker(this->points, tolerance);
}

#if 0
// This method simplifies all *lines* contained in the supplied area
template <class T>
void Polyline::simplify_by_visibility(const T &area)
{
    Points &pp = this->points;
    
    size_t s = 0;
    bool did_erase = false;
    for (size_t i = s+2; i < pp.size(); i = s + 2) {
        if (area.contains(Line(pp[s], pp[i]))) {
            pp.erase(pp.begin() + s + 1, pp.begin() + i);
            did_erase = true;
        } else {
            ++s;
        }
    }
    if (did_erase)
        this->simplify_by_visibility(area);
}
template void Polyline::simplify_by_visibility<ExPolygon>(const ExPolygon &area);
template void Polyline::simplify_by_visibility<ExPolygonCollection>(const ExPolygonCollection &area);
#endif

void Polyline::split_at(const Point &point, Polyline* p1, Polyline* p2) const
{
    if (this->points.empty()) return;

    if (this->size() < 2) {
        *p1 = *this;
        p2->clear();
        return;
    }

    if (this->points.front() == point) {
        //FIXME why is p1 NOT empty as in the case above?
        *p1 = { point };
        *p2 = *this;
        return;
    }

    double min_dist2 = std::numeric_limits<double>::max();
    auto   min_point_it = this->points.cbegin();
    Point  prev = this->points.front();
    for (auto it = this->points.cbegin() + 1; it != this->points.cend(); ++it) {
        Point proj;
        if (double d2 = line_alg::distance_to_squared(Line(prev, *it), point, &proj); d2 < min_dist2) {
            min_dist2 = d2;
            min_point_it = it;
        }
        prev = *it;
    }

    p1->points.assign(this->points.cbegin(), min_point_it);
    if (p1->points.back() != point)
        p1->points.emplace_back(point);
    
    p2->points = { point };
    if (*min_point_it == point)
        ++ min_point_it;
    p2->points.insert(p2->points.end(), min_point_it, this->points.cend());
}

bool Polyline::is_straight() const
{
    // Check that each segment's direction is equal to the line connecting
    // first point and last point. (Checking each line against the previous
    // one would cause the error to accumulate.)
    double dir = Line(this->first_point(), this->last_point()).direction();
    for (const auto &line: this->lines())
        if (! line.parallel_to(dir))
            return false;
    return true;
}

BoundingBox get_extents(const Polyline &polyline)
{
    return polyline.bounding_box();
}

BoundingBox get_extents(const Polylines &polylines)
{
    BoundingBox bb;
    if (! polylines.empty()) {
        bb = polylines.front().bounding_box();
        for (size_t i = 1; i < polylines.size(); ++ i)
            bb.merge(polylines[i].points);
    }
    return bb;
}

// Return True when erase some otherwise False.
bool remove_same_neighbor(Polyline &polyline) {
    Points &points = polyline.points;
    if (points.empty())
        return false;
    auto last = std::unique(points.begin(), points.end());

    // no duplicits
    if (last == points.end())
        return false;

    points.erase(last, points.end());
    return true;
}

bool remove_same_neighbor(Polylines &polylines){
    if (polylines.empty())
        return false;
    bool exist = false;
    for (Polyline &polyline : polylines)
        exist |= remove_same_neighbor(polyline);
    // remove empty polylines
    polylines.erase(std::remove_if(polylines.begin(), polylines.end(), [](const Polyline &p) { return p.points.size() <= 1; }), polylines.end());
    return exist;
}


const Point& leftmost_point(const Polylines &polylines)
{
    if (polylines.empty())
        throw Slic3r::InvalidArgument("leftmost_point() called on empty Polylines");
    Polylines::const_iterator it = polylines.begin();
    const Point *p = &it->leftmost_point();
    for (++ it; it != polylines.end(); ++it) {
        const Point *p2 = &it->leftmost_point();
        if (p2->x() < p->x())
            p = p2;
    }
    return *p;
}

bool remove_degenerate(Polylines &polylines)
{
    bool modified = false;
    size_t j = 0;
    for (size_t i = 0; i < polylines.size(); ++ i) {
        if (polylines[i].points.size() >= 2) {
            if (j < i) 
                std::swap(polylines[i].points, polylines[j].points);
            ++ j;
        } else
            modified = true;
    }
    if (j < polylines.size())
        polylines.erase(polylines.begin() + j, polylines.end());
    return modified;
}

std::pair<int, Point> foot_pt(const Points &polyline, const Point &pt)
{
    if (polyline.size() < 2)
        return std::make_pair(-1, Point(0, 0));

    auto  d2_min  = std::numeric_limits<double>::max();
    Point foot_pt_min;
    Point prev = polyline.front();
    auto  it = polyline.begin();
    auto  it_proj = polyline.begin();
    for (++ it; it != polyline.end(); ++ it) {
        Point foot_pt;
        if (double d2 = line_alg::distance_to_squared(Line(prev, *it), pt, &foot_pt); d2 < d2_min) {
            d2_min      = d2;
            foot_pt_min = foot_pt;
            it_proj     = it;
        }
        prev = *it;
    }
    return std::make_pair(int(it_proj - polyline.begin()) - 1, foot_pt_min);
}

ThickLines ThickPolyline::thicklines() const
{
    ThickLines lines;
    if (this->points.size() >= 2) {
        lines.reserve(this->points.size() - 1);
        for (size_t i = 0; i + 1 < this->points.size(); ++ i)
            lines.emplace_back(this->points[i], this->points[i + 1], this->points_width[i], this->points_width[i + 1]);
    }
    return lines;
}

// Removes the given distance from the end of the ThickPolyline
void ThickPolyline::clip_end(coordf_t distance)
{
    assert(this->points_width.size() == this->points.size());
    if (! this->empty()) {
        while (distance > 0) {
            Vec2d    last_point = this->back().cast<double>();
            coord_t last_width = this->points_width.back();
            this->points.pop_back();
            this->points_width.pop_back();
            assert(this->points_width.size() == this->points.size());
            if (this->points.empty())
                break;

            assert(this->points_width.size() == this->points.size());
            Vec2d    vec            = this->back().cast<double>() - last_point;
            coordf_t width_diff     = this->points_width.back() - last_width;
            coordf_t   vec_length_sqr = vec.squaredNorm();
            if (vec_length_sqr > distance * distance) {
                coordf_t t = (distance / std::sqrt(vec_length_sqr));
                this->points.emplace_back((last_point + vec * t).cast<coord_t>());
                this->points_width.emplace_back(last_width + width_diff * t);
                assert(this->points_width.size() == this->points.size());
                return;
            }

        distance -= std::sqrt(vec_length_sqr);
        }
    assert(this->points_width.size() == this->points.size());
    }
}
void ThickPolyline::extend_end(coordf_t distance)
{
    // relocate last point by extending the last segment by the specified length
    Vec2d v = (this->points.back() - *(this->points.end() - 2)).cast<coordf_t>().normalized();
    this->points.back() += (v * distance).cast<coord_t>();
}

void ThickPolyline::extend_start(coordf_t distance)
{
    // relocate first point by extending the first segment by the specified length
    Vec2d v = (this->points.front() - this->points[1]).cast<coordf_t>().normalized();
    this->points.front() += (v * distance).cast<coord_t>();
}

void ThickPolyline::start_at_index(int index)
{
    assert(index >= 0 && index < this->points.size());
    assert(this->points.front() == this->points.back() && this->points_width.front() == this->points_width.back());
    if (index != 0 && index + 1 != int(this->points.size()) && this->points.front() == this->points.back() && this->points_width.front() == this->points_width.back()) {
        this->points.pop_back();
        this->points_width.pop_back();
        assert(this->points.size() == this->points_width.size());
        std::rotate(this->points.begin(), this->points.begin() + index, this->points.end());
        std::rotate(this->points_width.begin(), this->points_width.begin() + index, this->points_width.end());
        this->points.emplace_back(this->points.front());
        this->points_width.emplace_back(this->points_width.front());
    }
}

double Polyline3::length() const
{
    double l = 0;
    for (size_t i = 1; i < this->points.size(); ++ i)
        l += (this->points[i] - this->points[i - 1]).cast<double>().norm();
    return l;
}

Lines3 Polyline3::lines() const
{
    Lines3 lines;
    if (points.size() >= 2)
    {
        lines.reserve(points.size() - 1);
        for (Points3::const_iterator it = points.begin(); it != points.end() - 1; ++it)
        {
            lines.emplace_back(*it, *(it + 1));
        }
    }
    return lines;
}

void concatThickPolylines(ThickPolylines& pp) {
    bool changes = true;
    while (changes){
        changes = false;
        //concat polyline if only 2 polyline at a point
        for (size_t i = 0; i < pp.size(); ++i) {
            ThickPolyline *polyline = &pp[i];
            if (polyline->front().coincides_with_epsilon(polyline->back())) {
                polyline->endpoints.first = false;
                polyline->endpoints.second = false;
                continue;
            }

            size_t id_candidate_first_point = -1;
            size_t id_candidate_last_point = -1;
            size_t nbCandidate_first_point = 0;
            size_t nbCandidate_last_point = 0;
            // find another polyline starting here
            for (size_t j = 0; j < pp.size(); ++j) {
                if (j == i) continue;
                ThickPolyline *other = &pp[j];
                if (polyline->back().coincides_with_epsilon(other->back())) {
                    id_candidate_last_point = j;
                    nbCandidate_last_point++;
                }
                if (polyline->back().coincides_with_epsilon(other->front())) {
                    id_candidate_last_point = j;
                    nbCandidate_last_point++;
                }
                if (polyline->front().coincides_with_epsilon(other->back())) {
                    id_candidate_first_point = j;
                    nbCandidate_first_point++;
                }
                if (polyline->front().coincides_with_epsilon(other->front())) {
                    id_candidate_first_point = j;
                    nbCandidate_first_point++;
                }
            }
            if (id_candidate_last_point == id_candidate_first_point && nbCandidate_first_point == 1 && nbCandidate_last_point == 1) {
                if (polyline->front().coincides_with_epsilon(pp[id_candidate_first_point].front())) pp[id_candidate_first_point].reverse();
                // it's a trap! it's a  loop!
                polyline->points.insert(polyline->points.end(), pp[id_candidate_first_point].points.begin() + 1, pp[id_candidate_first_point].points.end());
                polyline->points_width.insert(polyline->points_width.end(), pp[id_candidate_first_point].points_width.begin() + 1, pp[id_candidate_first_point].points_width.end());
                pp.erase(pp.begin() + id_candidate_first_point);
                changes = true;
                polyline->endpoints.first = false;
                polyline->endpoints.second = false;
            } else {

                if (nbCandidate_first_point == 1) {
                    if (polyline->front().coincides_with_epsilon(pp[id_candidate_first_point].front())) pp[id_candidate_first_point].reverse();
                    //concat at front
                    polyline->points_width[0] = std::max(polyline->points_width.front(), pp[id_candidate_first_point].points_width.back());
                    polyline->points.insert(polyline->points.begin(), pp[id_candidate_first_point].points.begin(), pp[id_candidate_first_point].points.end() - 1);
                    polyline->points_width.insert(polyline->points_width.begin(), pp[id_candidate_first_point].points_width.begin(), pp[id_candidate_first_point].points_width.end() - 1);
                    polyline->endpoints.first = pp[id_candidate_first_point].endpoints.first;
                    pp.erase(pp.begin() + id_candidate_first_point);
                    changes = true;
                    if (id_candidate_first_point < i) {
                        i--;
                        polyline = &pp[i];
                    }
                    if (id_candidate_last_point > id_candidate_first_point) {
                        id_candidate_last_point--;
                    }
                } else if (nbCandidate_first_point == 0) {
                    //update endpoint
                    polyline->endpoints.first = true;
                }
                if (nbCandidate_last_point == 1) {
                    if (polyline->back().coincides_with_epsilon(pp[id_candidate_last_point].back())) pp[id_candidate_last_point].reverse();
                    //concat at back
                    polyline->points_width[polyline->points_width.size() - 1] = std::max(polyline->points_width.back(), pp[id_candidate_last_point].points_width.front());
                    polyline->points.insert(polyline->points.end(), pp[id_candidate_last_point].points.begin() + 1, pp[id_candidate_last_point].points.end());
                    polyline->points_width.insert(polyline->points_width.end(), pp[id_candidate_last_point].points_width.begin() + 1, pp[id_candidate_last_point].points_width.end());
                    polyline->endpoints.second = pp[id_candidate_last_point].endpoints.second;
                    pp.erase(pp.begin() + id_candidate_last_point);
                    changes = true;
                    if (id_candidate_last_point < i) {
                        i--;
                        polyline = &pp[i];
                    }
                } else if (nbCandidate_last_point == 0) {
                    //update endpoint
                    polyline->endpoints.second = true;
                }

                if (polyline->back().coincides_with_epsilon(polyline->front())) {
                    //the concat has created a loop : update endpoints
                    polyline->endpoints.first = false;
                    polyline->endpoints.second = false;
                }
            }
        }
    }
}

//////////////// ArcPolyline ////////////////////////
// 
// Length of a smooth path.
//
//std::optional<Point> sample_path_point_at_distance_from_start(Geometry::ArcWelder::Path &path, double distance)
//{
//    if (distance >= 0) {
//        auto  it         = path.begin();
//        auto  end        = path.end();
//        Point prev_point = it->point;
//        for (++it; it != end; ++it) {
//            Point point = it->point;
//            if (it->linear()) {
//                // Linear segment
//                Vec2d  v    = (point - prev_point).cast<double>();
//                double lsqr = v.squaredNorm();
//                if (lsqr > sqr(distance))
//                    return std::make_optional<Point>(prev_point + (v * (distance / sqrt(lsqr))).cast<coord_t>());
//                distance -= sqrt(lsqr);
//            } else {
//                // Circular segment
//                float  angle = Geometry::ArcWelder::arc_angle(prev_point.cast<float>(), point.cast<float>(), it->radius);
//                double len   = std::abs(it->radius) * angle;
//                if (len > distance) {
//                    // Rotate the segment end point in reverse towards the start point.
//                    return std::make_optional<Point>(
//                        prev_point.rotated(-angle * (distance / len),
//                                            Geometry::ArcWelder::arc_center(prev_point.cast<float>(), point.cast<float>(), it->radius,
//                                                                            it->ccw())
//                                                .cast<coord_t>()));
//                }
//                distance -= len;
//            }
//            if (distance < 0)
//                return std::make_optional<Point>(point);
//            prev_point = point;
//        }
//    }
//    // Failed.
//    return {};
//}
//
//
//std::optional<Point> sample_path_point_at_distance_from_end(Geometry::ArcWelder::Path &path, double distance)
//{
//    if (distance >= 0) {
//        auto  it         = path.begin();
//        auto  end        = path.end();
//        Point prev_point = it->point;
//        for (++it; it != end; ++it) {
//            Point point = it->point;
//            if (it->linear()) {
//                // Linear segment
//                Vec2d  v    = (point - prev_point).cast<double>();
//                double lsqr = v.squaredNorm();
//                if (lsqr > sqr(distance))
//                    return std::make_optional<Point>(prev_point + (v * (distance / sqrt(lsqr))).cast<coord_t>());
//                distance -= sqrt(lsqr);
//            } else {
//                // Circular segment
//                float  angle = Geometry::ArcWelder::arc_angle(prev_point.cast<float>(), point.cast<float>(), it->radius);
//                double len   = std::abs(it->radius) * angle;
//                if (len > distance) {
//                    // Rotate the segment end point in reverse towards the start point.
//                    return std::make_optional<Point>(
//                        prev_point.rotated(-angle * (distance / len),
//                                            Geometry::ArcWelder::arc_center(prev_point.cast<float>(), point.cast<float>(), it->radius,
//                                                                            it->ccw())
//                                                .cast<coord_t>()));
//                }
//                distance -= len;
//            }
//            if (distance < 0)
//                return std::make_optional<Point>(point);
//            prev_point = point;
//        }
//    }
//    // Failed.
//    return {};
//}

bool not_arc(const ArcPolyline& arcs)
{
    for (const Geometry::ArcWelder::Segment &s : arcs.get_arc())
        if (s.radius > 0)
            return false;
    return true;
}

void ArcPolyline::append(const Points &src)
{
    for (const Point &point : src)
        m_path.emplace_back(point, 0.f, Geometry::ArcWelder::Orientation::Unknown);
        //m_path.push_back(Geometry::ArcWelder::Segment(point, 0.f, Geometry::ArcWelder::Orientation::Unknown));
}
void ArcPolyline::append(Points &&src)
{
    for (Point &point : src)
        m_path.emplace_back(std::move(point), 0, Geometry::ArcWelder::Orientation::Unknown);
}
void ArcPolyline::append(const Points::const_iterator &begin, const Points::const_iterator &end)
{
    Points::const_iterator it = begin;
    while (it != end) {
        m_path.emplace_back(*it, 0, Geometry::ArcWelder::Orientation::Unknown);
        ++it;
    }
}
void ArcPolyline::append(const ArcPolyline &src)
{
    this->m_only_strait &= src.m_only_strait;
    if (m_path.empty()) {
        m_path = std::move(src.m_path);
    } else {
        m_path.reserve(m_path.size() + src.m_path.size());
        std::move(src.m_path.begin(), src.m_path.end(), std::back_inserter(m_path));
    }
}
void ArcPolyline::append(ArcPolyline &&src)
{
    this->m_only_strait &= src.m_only_strait;
    if (m_path.empty()) {
        m_path = std::move(src.m_path);
    } else {
        m_path.reserve(m_path.size() + src.m_path.size());
        m_path.insert(m_path.end(), std::make_move_iterator(src.m_path.begin()), std::make_move_iterator(src.m_path.end()));
    }
}

void ArcPolyline::translate(const Vector &vector)
{
    for (auto &seg : m_path)
        seg.point += vector;
}
void ArcPolyline::rotate(double angle)
{
    double cos_angle = cos(angle), sin_angle = sin(angle);
    for (auto &seg : this->m_path) {
        double cur_x  = double(seg.point.x());
        double cur_y  = double(seg.point.y());
        seg.point.x() = coord_t(round(cos_angle * cur_x - sin_angle * cur_y));
        seg.point.y() = coord_t(round(cos_angle * cur_y + sin_angle * cur_x));
    }
}

int ArcPolyline::find_point(const Point &point) const
{
    for (size_t idx = 0; idx < this->m_path.size(); ++idx)
        if (m_path[idx].point == point)
            return int(idx);
    return -1; // not found
}

int ArcPolyline::find_point(const Point &point, coordf_t epsilon) const
{
    if (this->m_only_strait) {
        if (epsilon == 0)
            return this->find_point(point);

        auto dist2_min = std::numeric_limits<double>::max();
        auto eps2      = epsilon * epsilon;
        int  idx_min   = -1;
        for (size_t idx = 0; idx < this->m_path.size(); ++idx) {
            double d2 = (this->m_path[idx].point - point).cast<double>().squaredNorm();
            if (d2 < dist2_min) {
                idx_min   = int(idx);
                dist2_min = d2;
            }
        }
        return dist2_min < eps2 ? idx_min : -1;
    } else {
        assert(false);// not satisfactory
        Geometry::ArcWelder::PathSegmentProjection result = Geometry::ArcWelder::point_to_path_projection(m_path, point);
        if (result.distance2 > epsilon * epsilon)
            return -1;
        return int(result.segment_id);
    }
}

bool ArcPolyline::at_least_length(coordf_t length) const
{
    for (size_t i = 1; length > 0 && i < m_path.size(); ++ i)
        length -= Geometry::ArcWelder::segment_length<double>(m_path[i - 1], m_path[i]);
    return length <= 0;
}

//for seams
std::pair<int, Point> ArcPolyline::foot_pt(const Point &pt) const
{
    if (m_path.size() < 2)
        return std::make_pair(-1, Point(0, 0));

    if (this->m_only_strait) {
        auto  d2_min = std::numeric_limits<double>::max();
        Point foot_pt_min;
        size_t foot_idx_min = 0;
        Point prev    = m_path.front().point;
        for (size_t idx = 1; idx < m_path.size(); ++idx) {
            Point foot_pt;
            if (double d2 = line_alg::distance_to_squared(Line(prev, m_path[idx].point), pt, &foot_pt); d2 < d2_min) {
                d2_min      = d2;
                foot_pt_min = foot_pt;
                foot_idx_min = idx;
            }
            prev = m_path[idx].point;
        }
        return std::make_pair(int(foot_idx_min) - 1, foot_pt_min);
    } else {
        Geometry::ArcWelder::PathSegmentProjection result = Geometry::ArcWelder::point_to_path_projection(m_path, pt);
        // check that if last point, then it's really the last point
        //assert(result.segment_id + 1 < m_path.size() ||
        //       (m_path.back().point.distance_to(pt) - std::sqrt(result.distance2)) < SCALED_EPSILON);
        // else check that if on strait segment, it's proj into it.
        assert(result.segment_id + 1 == m_path.size() || m_path[result.segment_id + 1].radius != 0 ||
               result.point.distance_to(
                   result.point.projection_onto(m_path[result.segment_id].point, m_path[result.segment_id + 1].point)) < SCALED_EPSILON);
        // else check that it's on the arc
        assert(result.segment_id + 1 == m_path.size() || m_path[result.segment_id + 1].radius == 0 ||
               Geometry::ArcWelder::point_to_path_projection({m_path[result.segment_id], m_path[result.segment_id + 1]}, result.point,
                                                             std::sqrt(result.distance2) + SCALED_EPSILON * 2)
                       .point.distance_to(result.point) < SCALED_EPSILON);
        assert(result.center == Point(0, 0) || m_path[result.segment_id + 1].radius != 0);
        assert(result.center != Point(0, 0) || m_path[result.segment_id + 1].radius == 0);
        // arc: projection
        return std::make_pair(int(result.segment_id), result.point);
    }
}


void ArcPolyline::pop_front()
{
    assert(m_only_strait);
    assert(m_path.size() > 2);
    m_path.erase(m_path.begin());
    if (!m_path.empty())
        m_path.front().radius = 0.f;
    if (!m_only_strait)
        m_only_strait = not_arc(*this);
}

void ArcPolyline::pop_back()
{
    assert(m_only_strait);
    assert(m_path.size() > 2);
    m_path.pop_back();
    if (!m_only_strait)
        m_only_strait = not_arc(*this);
}

void ArcPolyline::clip_start(coordf_t dist)
{
    Geometry::ArcWelder::clip_start(m_path, dist);
    if (!m_only_strait)
        m_only_strait = not_arc(*this);
}

void ArcPolyline::clip_end(coordf_t dist)
{
    Geometry::ArcWelder::clip_end(m_path, dist);
    if (!m_only_strait)
        m_only_strait = not_arc(*this);
}

void ArcPolyline::split_at(coordf_t distance, ArcPolyline &p1, ArcPolyline &p2) const
{
    if(m_path.empty()) return;
    p1.m_path.push_back(m_path.front());
    size_t idx = 1;
    while(distance > 0 && idx < m_path.size()) {
        const Geometry::ArcWelder::Segment current = m_path[idx];
        if (current.linear()) {
            // Linear segment
            Vec2d  v    = (current.point - p1.back()).cast<double>();
            double lsqr = v.squaredNorm();
            if (lsqr > sqr(distance)) {
                Point split_point = p1.back() + (v * (distance / sqrt(lsqr))).cast<coord_t>();
                p1.m_path.push_back({split_point, 0, Geometry::ArcWelder::Orientation::Unknown});
                p2.m_path.push_back({split_point, 0, Geometry::ArcWelder::Orientation::Unknown});
                // Length to go is zero.
                distance = 0;
            } else {
                p1.m_path.push_back(current);
                distance -= sqrt(lsqr);
            }
        } else {
            // Circular segment
            //double angle = Geometry::ArcWelder::arc_angle(path.back().point.cast<double>(), last.point.cast<double>(), last.radius);
            double angle = Geometry::ArcWelder::arc_angle(p1.back().cast<double>(), current.point.cast<double>(), current.radius);
            //double len   = std::abs(last.radius) * angle;
            double len   = std::abs(current.radius) * angle;
            if (len > distance) {
                // Rotate the segment end point in reverse towards the start point.
                if (current.ccw())
                    angle *= -1.;
                //path.push_back({
                //    last.point.rotated(angle * (distance / len),
                //        arc_center(path.back().point.cast<double>(), last.point.cast<double>(), double(last.radius), last.ccw()).cast<coord_t>()),
                //    last.radius, last.orientation });
                Point split_point = current.point.rotated(angle * (distance / len),
                        Geometry::ArcWelder::arc_center(p1.back().cast<double>(), current.point.cast<double>(), double(current.radius), current.ccw()).cast<coord_t>());
                p1.m_path.push_back({split_point, current.radius, current.orientation });
                p2.m_path.push_back({split_point, 0, Geometry::ArcWelder::Orientation::Unknown});
                // Length to go is zero.
                distance = 0;
            } else {
                p1.m_path.push_back(current);
                distance -= len;
            }
        }
        //increment
        ++idx;
    }
    //now fill p2
    while (idx < m_path.size()) {
        p2.m_path.push_back(m_path[idx]);
        // increment
        ++idx;
    }
}

void ArcPolyline::split_at(Point &point, ArcPolyline &p1, ArcPolyline &p2) const
{
    if (this->m_path.empty())
        return;

    if (this->size() < 2) {
        p1 = *this;
        p2.clear();
        return;
    }

    if (this->m_path.front().point == point) {
        p1.clear();
        p1.append(point);
        p2 = *this;
        return;
    }

    // 0 judge whether the point is on the polyline
    int index = this->find_point(point);
    if (index != -1) {
        // BBS: the split point is on the polyline, then easy
        split_at_index(index, p1, p2);
        point = p1.is_valid() ? p1.back() : p2.front();
        return;
    }

    //find the line to split at
    Geometry::ArcWelder::PathSegmentProjection result = Geometry::ArcWelder::point_to_path_projection(m_path, point);
    assert(result.center == Point(0, 0) && this->m_path[result.segment_id].radius == 0);
    assert(result.center != Point(0, 0) && this->m_path[result.segment_id].radius != 0);
    //split and update point
    p1.clear();
    p1.m_path.reserve(result.segment_id + 1);
    p1.m_path.insert(p1.m_path.begin(), this->m_path.begin(), this->m_path.begin() + result.segment_id + 1);
    p1.m_path.back().point = result.point;
    p1.m_only_strait       = not_arc(p1);
    p2.clear();
    p2.m_path.reserve(this->size() - result.segment_id);
    p2.m_path.insert(p2.m_path.begin(), this->m_path.begin() + result.segment_id, this->m_path.end());
    p2.m_path.front().point  = result.point;
    p2.m_path.front().radius = 0; // first point can't be an arc
    p2.m_only_strait         = not_arc(p2);

    point = result.point;
}

bool ArcPolyline::split_at_index(const size_t index, ArcPolyline &p1, ArcPolyline &p2) const
{
    assert(m_only_strait);
    if (index >= this->size())
        return false;

    if (index == 0) {
        p1.append(this->front());
        p2 = *this;
    } else if (index == this->size() - 1) {
        p2.m_path.insert(p2.m_path.begin(), Geometry::ArcWelder::Segment{this->back(), 0.f, Geometry::ArcWelder::Orientation::Unknown});
        p1 = *this;
    } else {
        p1.m_path.reserve(p1.m_path.size() + index + 1);
        p1.m_path.insert(p1.m_path.end(), this->m_path.begin(), this->m_path.begin() + index + 1);
        p1.m_only_strait = not_arc(p1);

        p2.m_path.reserve(p2.m_path.size() + this->size() - index);
        p2.m_path.insert(p2.m_path.begin(), this->m_path.begin() + index, this->m_path.end());
        p2.m_path.front().radius = 0; // first point can't be an arc
        p2.m_only_strait         = not_arc(p2);
    }
    return true;
}

//TODO: find a way to avoid duplication of code below
Point ArcPolyline::get_point_from_begin(coord_t distance) const {
    size_t idx = 1;
    while (distance > 0 && idx < m_path.size()) {
        const Geometry::ArcWelder::Segment last = m_path[idx];
        const Geometry::ArcWelder::Segment current = m_path[idx - 1];
        if (last.linear()) {
            // Linear segment
            Vec2d  v    = (current.point - last.point).cast<double>();
            double lsqr = v.squaredNorm();
            if (lsqr > sqr(distance)) {
                // Length to go is zero.
                return last.point + (v * (distance / sqrt(lsqr))).cast<coord_t>();
            }
            distance -= sqrt(lsqr);
        } else {
            // Circular segment
            double angle = Geometry::ArcWelder::arc_angle(current.point.cast<double>(), last.point.cast<double>(), last.radius);
            double len   = std::abs(last.radius) * angle;
            if (len > distance) {
                // Rotate the segment end point in reverse towards the start point.
                if (last.ccw())
                    angle *= -1.;
                return last.point.rotated(angle * (distance / len),
                        Geometry::ArcWelder::arc_center(current.point.cast<double>(), last.point.cast<double>(), double(last.radius), last.ccw()).cast<coord_t>());
            }
            distance -= len;
        }
        ++idx;
    }

    // Return remaining distance to go.
    assert(distance >= 0);
    return m_path.front().point;
}

Point ArcPolyline::get_point_from_end(coord_t distance) const {
    size_t idx = m_path.size() - 1;
    while (distance > 0 && idx > 0) {
        const Geometry::ArcWelder::Segment last = m_path[idx];
        const Geometry::ArcWelder::Segment current = m_path[idx - 1];
        if (last.linear()) {
            // Linear segment
            Vec2d  v    = (current.point - last.point).cast<double>();
            double lsqr = v.squaredNorm();
            if (lsqr > sqr(distance)) {
                // Length to go is zero.
                return last.point + (v * (distance / sqrt(lsqr))).cast<coord_t>();
            }
            distance -= sqrt(lsqr);
        } else {
            // Circular segment
            double angle = Geometry::ArcWelder::arc_angle(current.point.cast<double>(), last.point.cast<double>(), last.radius);
            double len   = std::abs(last.radius) * angle;
            if (len > distance) {
                // Rotate the segment end point in reverse towards the start point.
                if (last.ccw())
                    angle *= -1.;
                return last.point.rotated(angle * (distance / len),
                        Geometry::ArcWelder::arc_center(current.point.cast<double>(), last.point.cast<double>(), double(last.radius), last.ccw()).cast<coord_t>());
            }
            distance -= len;
        }
        --idx;
    }

    // Return remaining distance to go.
    assert(distance >= 0);
    return m_path.front().point;
}

void ArcPolyline::set_front(const Point &p) {
    assert(!m_path.empty());
    m_path.front().point = p;
    if(m_path.size()>1)
        m_path[1].radius = 0.f;
}

void ArcPolyline::set_back(const Point &p) {
    assert(!m_path.empty());
    m_path.back().point = p;
    m_path.back().radius = 0.f;
}

Polyline ArcPolyline::to_polyline(coord_t deviation/*=0*/) const {
    Polyline poly_out;
    if (!empty()) {
        assert(m_path.front().radius == 0);
        if (deviation > 0 || m_only_strait || m_path.size() < 2) {
            for (const Geometry::ArcWelder::Segment &seg : m_path)
                if (seg.radius == 0)
                    poly_out.append(seg.point);
                else
                    poly_out.append(Geometry::ArcWelder::arc_discretize(poly_out.back(), seg.point, seg.radius,
                                                                        seg.orientation == Geometry::ArcWelder::Orientation::CCW,
                                                                        (double) deviation));
        } else {
            //find a good deviation value
            //max: a dist
            float deviation2 = m_path[1].point.distance_to_square(m_path.front().point) / 10;
            //now reduce it to the min of dist/10 or radius/100
            for (size_t i = 2; i < m_path.size() ; i++)
                if (m_path[i].radius == 0)
                    deviation2 = std::min(deviation2, (float)m_path[i-1].point.distance_to_square(m_path[i].point));
                else
                    deviation2 = std::min(deviation2, m_path[i].radius * m_path[i].radius * 0.0001f);
            // don't go too low
            deviation = std::max(coord_t(std::sqrt(deviation2)), SCALED_EPSILON * 10);

            for (const Geometry::ArcWelder::Segment &seg : m_path)
                if (seg.radius == 0)
                    poly_out.append(seg.point);
                else
                    poly_out.append(Geometry::ArcWelder::arc_discretize(poly_out.back(), seg.point, seg.radius,
                                                                        seg.orientation == Geometry::ArcWelder::Orientation::CCW,
                                                                        (double) deviation));
        }
    }
    return poly_out;
}


Geometry::ArcWelder::Path ArcPolyline::_from_polyline(const Points &poly)
{
    Geometry::ArcWelder::Path path;
    for (const Point &point : poly)
        path.emplace_back(std::move(point), 0, Geometry::ArcWelder::Orientation::Unknown);
    return path;
}
Geometry::ArcWelder::Path ArcPolyline::_from_polyline(std::initializer_list<Point> poly)
{
    Geometry::ArcWelder::Path path;
    for (const Point &point : poly)
        path.emplace_back(std::move(point), 0, Geometry::ArcWelder::Orientation::Unknown);
    return path;
}

// douglas_peuker and create arc if with_fitting_arc
void ArcPolyline::simplify(coordf_t tolerance, ArcFittingType with_fitting_arc, double fit_percent_tolerance)
{
    if (with_fitting_arc != ArcFittingType::Disabled) {
        // BBS: do arc fit first, then use DP simplify to handle the straight part to reduce point.
        Points pts;
        Geometry::ArcWelder::Path path;
        // do only section without arcs
        size_t idx_start_path = 0, idx_end_mpath;
        for (idx_end_mpath = 1; idx_end_mpath < m_path.size(); ++idx_end_mpath) {
            path.push_back(m_path[idx_end_mpath]);
            if (m_path[idx_end_mpath].radius != 0) {
                // less than 3 points: don't use
                if (pts.size() > 2) {
                    // do arc fitting
                    if (with_fitting_arc == ArcFittingType::Bambu) {
                        // === use BBS method ===
                        std::vector<Slic3r::Geometry::PathFittingData> result;
                        Slic3r::Geometry::ArcFitter::do_arc_fitting_and_simplify(pts, result, tolerance, fit_percent_tolerance);
                        // remove strait sections
                        path.resize(idx_start_path);
                        // transform PathFittingData into path
                        size_t last_end = 0;
                        for (Slic3r::Geometry::PathFittingData data : result) {
                            assert(data.start_point_index == last_end);
                            if (data.path_type == Slic3r::Geometry::EMovePathType::Linear_move) {
                                // add strait section
                                assert(pts[data.start_point_index] == path.back().point);
                                for (size_t idx_pts = data.start_point_index + 1; idx_pts < data.end_point_index; ++idx_pts) {
                                    path.emplace_back(pts[idx_pts], 0, Geometry::ArcWelder::Orientation::Unknown);
                                }
                            } else if (data.path_type == Slic3r::Geometry::EMovePathType::Arc_move_cw ||
                                       data.path_type == Slic3r::Geometry::EMovePathType::Arc_move_ccw) {
                                assert(data.arc_data.start_point == path.back().point);
                                // now the arc section
                                path.emplace_back(data.arc_data.end_point, data.arc_data.angle_radians,
                                                  data.arc_data.direction == Slic3r::Geometry::ArcDirection::Arc_Dir_CCW ?
                                                      Geometry::ArcWelder::Orientation::CCW :
                                                      Geometry::ArcWelder::Orientation::CW);
                            } else {
                                assert(false);
                            }
                        }
                    } else /* if (with_fitting_arc == ArcFittingType::EmitCenter)*/ {
                        // === use ArcWelder ===
                        Geometry::ArcWelder::Path result = Geometry::ArcWelder::fit_path(pts, tolerance, fit_percent_tolerance);
                        assert(result.size() > 1);
                        assert(result.front().radius == 0);
                        assert(path.back().point == result.front().point);
                        path.insert(path.end(), result.begin() + 1, result.end());
                    }
                    assert(m_path[idx_end_mpath].point == path.back().point);
                }
                idx_start_path = path.size();
                pts.clear();
            }
            pts.push_back(m_path[idx_end_mpath].point);
        }
        this->m_only_strait = not_arc(*this);
    } else {
        douglas_peucker<double>(this->m_path.begin(), this->m_path.end(), this->m_path.begin(), tolerance,
                                        [](const Geometry::ArcWelder::Segment &s) { return s.point; });
    }
}

//////////////// PolylineOrArc ////////////////////////



// removes the given distance from the end of the polyline
void PolylineOrArc::clip_end(coordf_t distance)
{
    bool last_point_inserted = false;
    size_t remove_after_index = MultiPoint::size();
    while (distance > 0) {
        Vec2d  last_point = this->last_point().cast<coordf_t>();
        this->points.pop_back();
        remove_after_index--;
        if (this->points.empty()) {
            this->m_fitting_result.clear();
            return;
        }
        Vec2d  v = this->last_point().cast<coordf_t>() - last_point;
        coordf_t lsqr = v.squaredNorm();
        if (lsqr > distance * distance) {
            this->points.emplace_back((last_point + v * (distance / sqrt(lsqr))).cast<coord_t>());
            last_point_inserted = true;
            break;
        }
        distance -= sqrt(lsqr);
    }

    //BBS: don't need to clip fitting result if it's empty
    if (m_fitting_result.empty())
        return;
    while (!m_fitting_result.empty() && m_fitting_result.back().start_point_index >= remove_after_index)
        m_fitting_result.pop_back();
    if (!m_fitting_result.empty()) {
        //BBS: last remaining segment is arc move, then clip the arc at last point
        if (m_fitting_result.back().path_type == Slic3r::Geometry::EMovePathType::Arc_move_ccw
            || m_fitting_result.back().path_type == Slic3r::Geometry::EMovePathType::Arc_move_cw) {
            if (m_fitting_result.back().arc_data.clip_end(this->last_point()))
                //BBS: succeed to clip arc, then update the last point
                this->points.back() = m_fitting_result.back().arc_data.end_point;
            else
                //BBS: Failed to clip arc, then back to linear move
                m_fitting_result.back().path_type = Slic3r::Geometry::EMovePathType::Linear_move;
        }
        m_fitting_result.back().end_point_index = this->points.size() - 1;
    }
    assert(this->m_fitting_result.empty() || this->m_fitting_result.back().end_point_index < this->points.size());
}

void PolylineOrArc::clip_start(coordf_t distance)
{
    this->reverse();
    this->clip_end(distance);
    if (this->points.size() >= 2)
        this->reverse();
}

void PolylineOrArc::clip_last_point() {
    assert(!empty());
    this->points.erase(this->points.end()-1);
    if (!m_fitting_result.empty()) {
        //BBS: last remaining segment is arc move, then clip the arc at last point
        if (m_fitting_result.back().path_type == Slic3r::Geometry::EMovePathType::Arc_move_ccw
            || m_fitting_result.back().path_type == Slic3r::Geometry::EMovePathType::Arc_move_cw) {
            if (m_fitting_result.back().arc_data.clip_end(this->last_point()))
                //BBS: succeed to clip arc, then update the last point
                this->points.back() = m_fitting_result.back().arc_data.end_point;
            else
                //BBS: Failed to clip arc, then back to linear move
                m_fitting_result.back().path_type = Slic3r::Geometry::EMovePathType::Linear_move;
        }
        m_fitting_result.back().end_point_index = this->points.size() - 1;
    }
    while (!m_fitting_result.empty() && m_fitting_result.back().start_point_index >= m_fitting_result.back().end_point_index) {
        m_fitting_result.pop_back();
    }
    assert(this->m_fitting_result.empty() || this->m_fitting_result.back().end_point_index < this->points.size());
}

void PolylineOrArc::clip_first_point() {
    assert(!empty());
    assert(points.size() > 2);
    this->points.erase(this->points.begin());
    if (!m_fitting_result.empty()) {
        if (m_fitting_result.front().path_type == Slic3r::Geometry::EMovePathType::Arc_move_ccw
            || m_fitting_result.front().path_type == Slic3r::Geometry::EMovePathType::Arc_move_cw) {
            if (m_fitting_result.front().arc_data.clip_start(this->first_point())){
                //BBS: succeed to clip arc, then update the last point
                this->points.front() = m_fitting_result.front().arc_data.start_point;
            } else {
                //BBS: Failed to clip arc, then back to linear move
                m_fitting_result.front().path_type = Slic3r::Geometry::EMovePathType::Linear_move;
            }
        }
        //move m_fitting_result indexes.
        for (auto& fit : m_fitting_result) {
            fit.start_point_index = fit.start_point_index == 0 ? 0 : fit.start_point_index - 1;
            --fit.end_point_index;
        }
        while (!m_fitting_result.empty() && m_fitting_result.front().start_point_index >= m_fitting_result.front().end_point_index) {
            m_fitting_result.erase(m_fitting_result.begin());
        }
        assert(m_fitting_result.front().end_point_index == this->points.size() - 1);
    }
}

void PolylineOrArc::simplify(coordf_t tolerance, ArcFittingType with_fitting_arc, double fit_percent_tolerance)
{
    if (with_fitting_arc == ArcFittingType::Bambu) {
        //BBS: do arc fit first, then use DP simplify to handle the straight part to reduce point.
        Slic3r::Geometry::ArcFitter::do_arc_fitting_and_simplify(this->points, this->m_fitting_result, tolerance, fit_percent_tolerance);
    } else {
        this->points = MultiPoint::douglas_peucker(this->points, tolerance);
        this->m_fitting_result.clear();
    }
    assert(this->m_fitting_result.empty() || this->m_fitting_result.back().end_point_index < this->points.size());
}

void PolylineOrArc::reverse()
{
    //BBS: reverse points
    MultiPoint::reverse();
    //BBS: reverse the m_fitting_result
    if (!this->m_fitting_result.empty()) {
        for (size_t i = 0; i < this->m_fitting_result.size(); i++) {
            std::swap(m_fitting_result[i].start_point_index, m_fitting_result[i].end_point_index);
            m_fitting_result[i].start_point_index = MultiPoint::size() - 1 - m_fitting_result[i].start_point_index;
            m_fitting_result[i].end_point_index = MultiPoint::size() - 1 - m_fitting_result[i].end_point_index;
            if (m_fitting_result[i].is_arc_move())
                m_fitting_result[i].reverse_arc_path();
        }
        std::reverse(this->m_fitting_result.begin(), this->m_fitting_result.end());
    }
    assert(this->m_fitting_result.empty() || this->m_fitting_result.back().end_point_index < this->points.size());
}


/* this method returns a collection of points picked on the polygon contour
   so that they are evenly spaced according to the input distance */
Points PolylineOrArc::equally_spaced_points(coordf_t distance) const
{
    Points points;
    points.emplace_back(this->first_point());
    double len = 0;

    for (Points::const_iterator it = this->points.begin() + 1; it != this->points.end(); ++it) {
        Vec2d  p1 = (it - 1)->cast<double>();
        Vec2d  v = it->cast<double>() - p1;
        coordf_t segment_length = v.norm();
        len += segment_length;
        if (len < distance)
            continue;
        if (len == distance) {
            points.emplace_back(*it);
            len = 0;
            continue;
        }
        coordf_t take = segment_length - (len - distance);  // how much we take of this segment
        points.emplace_back((p1 + v * (take / v.norm())).cast<coord_t>());
        --it;
        len = -take;
    }
    return points;
}

void PolylineOrArc::split_at(Point& point, PolylineOrArc* p1, PolylineOrArc* p2) const
{
    if (this->points.empty()) return;

    if (this->size() < 2) {
        *p1 = *this;
        p2->clear();
        return;
    }

    if (this->points.front() == point) {
        *p1 = PolylineOrArc{ point };
        *p2 = *this;
        return;
    }

    //0 judge whether the point is on the polyline
    int index = this->find_point(point);
    if (index != -1) {
        //BBS: the split point is on the polyline, then easy
        split_at_index(index, p1, p2);
        point = p1->is_valid() ? p1->last_point() : p2->first_point();
        return;
    }

    //1 find the line to split at
    size_t line_idx = 0;
    Point best_p = this->first_point();
    double min_dist_sqr = best_p.distance_to_square(point);
    Lines lines = this->lines();
    for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line) {
        Point p_tmp = point.projection_onto(line->a, line->b);
        if (p_tmp.distance_to_square(point) < min_dist_sqr) {
            best_p = p_tmp;
            min_dist_sqr = best_p.distance_to_square(point);
            line_idx = line - lines.begin();
        }
    }

    //2 judge whether the cloest point is one vertex of polyline.
    //  and spilit the polyline at different index
    index = this->find_point(best_p);
    if (index != -1)
    {
        this->split_at_index(index, p1, p2);
        if (p1->points.back() != point)
            p1->append(point);
        if (p2->points.front() != point)
            p2->append_before(point);
    } else {
        PolylineOrArc temp;
        this->split_at_index(line_idx, p1, &temp);
        p1->append(point);
        this->split_at_index(line_idx + 1, &temp, p2);
        p2->append_before(point);
    }
    assert(this->m_fitting_result.empty() || this->m_fitting_result.back().end_point_index < this->points.size());
}

bool PolylineOrArc::split_at_index(const size_t index, PolylineOrArc* p1, PolylineOrArc* p2) const
{
    if (index > this->size() - 1)
        return false;

    if (index == 0) {
        p1->clear();
        p1->append(this->first_point());
        *p2 = *this;
    } else if (index == this->size() - 1) {
        p2->clear();
        p2->append(this->last_point());
        *p1 = *this;
    } else {
        //BBS: spilit first part
        p1->clear();
        p1->points.reserve(index + 1);
        p1->points.insert(p1->begin(), this->begin(), this->begin() + index + 1);
        Point new_endpoint;
        if (this->split_fitting_result_before_index(index, new_endpoint, p1->m_fitting_result))
            p1->points.back() = new_endpoint;

        p2->clear();
        p2->points.reserve(this->size() - index);
        p2->points.insert(p2->begin(), this->begin() + index, this->end());
        Point new_startpoint;
        if (this->split_fitting_result_after_index(index, new_startpoint, p2->m_fitting_result))
            p2->points.front() = new_startpoint;
    }
    assert(this->m_fitting_result.empty() || this->m_fitting_result.back().end_point_index < this->points.size());
    return true;
}

void PolylineOrArc::append(const PolylineOrArc& src)
{
    if (!src.is_valid()) return;

    if (this->points.empty()) {
        this->points = src.points;
        this->m_fitting_result = src.m_fitting_result;
    } else {
        //BBS: append the first point to create connection first, update the fitting data as well
        if (src.front() != back()) {
            this->append(src.front());
            if (!this->m_fitting_result.empty()) {
                assert(size() >= 2);
                this->m_fitting_result.emplace_back(Slic3r::Geometry::PathFittingData{ this->points.size() - 2, this->points.size() - 1, Slic3r::Geometry::EMovePathType::Linear_move, Slic3r::Geometry::ArcSegment() });
            }
        }
        //BBS: append a polyline which has fitting data to a polyline without fitting data.
        //Then create a fake fitting data first, so that we can keep the fitting data in last polyline
        if (this->m_fitting_result.empty() &&
            !src.m_fitting_result.empty()) {
            this->m_fitting_result.emplace_back(Slic3r::Geometry::PathFittingData{ 0, this->points.size() - 1, Slic3r::Geometry::EMovePathType::Linear_move, Slic3r::Geometry::ArcSegment() });
        }
        //BBS: then append the remain points
        MultiPoint::append(src.points.begin() + 1, src.points.end());
        //BBS: finally append the fitting data
        append_fitting_result_after_append_polyline(src);
    }
    assert(this->m_fitting_result.empty() || this->m_fitting_result.back().end_point_index < this->points.size());
}

void PolylineOrArc::append(PolylineOrArc&& src)
{
    if (!src.is_valid()) return;

    if (this->points.empty()) {
        this->points = std::move(src.points);
        this->m_fitting_result = std::move(src.m_fitting_result);
    } else {
        //BBS: append the first point to create connection first, update the fitting date as well
        if (last_point() != src.first_point()) {
            this->append(src.points[0]);
        }
        //BBS: append a polyline which has fitting data to a polyline without fitting data.
        //Then create a fake fitting data first, so that we can keep the fitting data in last polyline
        if (this->m_fitting_result.empty() &&
            !src.m_fitting_result.empty()) {
            this->m_fitting_result.emplace_back(Slic3r::Geometry::PathFittingData{ 0, this->points.size() - 1, Slic3r::Geometry::EMovePathType::Linear_move, Slic3r::Geometry::ArcSegment() });
        }
        //BBS: then append the remain points
        MultiPoint::append(src.points.begin() + 1, src.points.end());
        //BBS: finally append the fitting data
        append_fitting_result_after_append_polyline(src);
        src.points.clear();
        src.m_fitting_result.clear();
    }
    assert(this->m_fitting_result.empty() || this->m_fitting_result.back().end_point_index < this->points.size());
}

void PolylineOrArc::append_fitting_result_after_append_points() {
    if (!m_fitting_result.empty()) {
        if (m_fitting_result.back().is_linear_move()) {
            m_fitting_result.back().end_point_index = this->points.size() - 1;
        } else {
            size_t new_start = m_fitting_result.back().end_point_index;
            size_t new_end = this->points.size() - 1;
            if (new_start != new_end)
                m_fitting_result.emplace_back(Slic3r::Geometry::PathFittingData{ new_start, new_end, Slic3r::Geometry::EMovePathType::Linear_move, Slic3r::Geometry::ArcSegment() });
        }
    }
    assert(this->m_fitting_result.empty() || this->m_fitting_result.back().end_point_index < this->points.size());
}

void PolylineOrArc::append_fitting_result_after_append_polyline(const PolylineOrArc& src)
{
    if (!this->m_fitting_result.empty()) {
        //BBS: offset and save the m_fitting_result from src polyline
        if (!src.m_fitting_result.empty()) {
            size_t old_size = this->m_fitting_result.size();
            size_t index_offset = this->m_fitting_result.back().end_point_index;
            this->m_fitting_result.insert(this->m_fitting_result.end(), src.m_fitting_result.begin(), src.m_fitting_result.end());
            for (size_t i = old_size; i < this->m_fitting_result.size(); i++) {
                this->m_fitting_result[i].start_point_index += index_offset;
                this->m_fitting_result[i].end_point_index += index_offset;
            }
        } else {
            //BBS: the append polyline has no fitting data, then append as linear move directly
            size_t new_start = this->m_fitting_result.back().end_point_index;
            size_t new_end = this->size() - 1;
            if (new_start != new_end)
                this->m_fitting_result.emplace_back(Slic3r::Geometry::PathFittingData{ new_start, new_end, Slic3r::Geometry::EMovePathType::Linear_move, Slic3r::Geometry::ArcSegment() });
        }
    }
    assert(this->m_fitting_result.empty() || this->m_fitting_result.back().end_point_index < this->points.size());
}

void PolylineOrArc::reset_to_linear_move()
{
    this->m_fitting_result.clear();
    m_fitting_result.emplace_back(Slic3r::Geometry::PathFittingData{ 0, points.size() - 1, Slic3r::Geometry::EMovePathType::Linear_move, Slic3r::Geometry::ArcSegment() });
    this->m_fitting_result.shrink_to_fit();
    assert(this->m_fitting_result.empty() || this->m_fitting_result.back().end_point_index < this->points.size());
}

bool PolylineOrArc::split_fitting_result_before_index(const size_t index, Point& new_endpoint, std::vector<Slic3r::Geometry::PathFittingData>& data) const
{
    data.clear();
    new_endpoint = this->points[index];
    if (!this->m_fitting_result.empty()) {
        //BBS: max size
        data.reserve(this->m_fitting_result.size());
        //BBS: save fitting result before index
        for (size_t i = 0; i < this->m_fitting_result.size(); i++)
        {
            if (this->m_fitting_result[i].start_point_index < index)
                data.push_back(this->m_fitting_result[i]);
            else
                break;
        }

        if (!data.empty()) {
            //BBS: need to clip the arc and generate new end point
            if (data.back().is_arc_move() && data.back().end_point_index > index) {
                if (!data.back().arc_data.clip_end(this->points[index]))
                    //BBS: failed to clip arc, then return to be linear move
                    data.back().path_type = Slic3r::Geometry::EMovePathType::Linear_move;
                else
                    //BBS: succeed to clip arc, then update and return the new end point
                    new_endpoint = data.back().arc_data.end_point;
            }
            data.back().end_point_index = index;
        }
        data.shrink_to_fit();
        assert(this->m_fitting_result.empty() || this->m_fitting_result.back().end_point_index < this->points.size());
        return true;
    }
    assert(this->m_fitting_result.empty() || this->m_fitting_result.back().end_point_index < this->points.size());
    return false;
}
bool PolylineOrArc::split_fitting_result_after_index(const size_t index, Point& new_startpoint, std::vector<Slic3r::Geometry::PathFittingData>& data) const
{
    data.clear();
    new_startpoint = this->points[index];
    if (!this->m_fitting_result.empty()) {
        data.reserve(this->m_fitting_result.size());
        for (size_t i = 0; i < this->m_fitting_result.size(); i++) {
            if (this->m_fitting_result[i].end_point_index > index)
                data.push_back(this->m_fitting_result[i]);
        }
        if (!data.empty()) {
            for (size_t i = 0; i < data.size(); i++) {
                if (i != 0) {
                    data[i].start_point_index -= index;
                    data[i].end_point_index -= index;
                } else {
                    data[i].end_point_index -= index;
                    //BBS: need to clip the arc and generate new start point
                    if (data.front().is_arc_move() && data.front().start_point_index < index) {
                        if (!data.front().arc_data.clip_start(this->points[index]))
                            //BBS: failed to clip arc, then return to be linear move
                            data.front().path_type = Slic3r::Geometry::EMovePathType::Linear_move;
                        else
                            //BBS: succeed to clip arc, then update and return the new start point
                            new_startpoint = data.front().arc_data.start_point;
                    }
                    data[i].start_point_index = 0;
                }
            }
        }
        data.shrink_to_fit();
        assert(this->m_fitting_result.empty() || this->m_fitting_result.back().end_point_index < this->points.size());
        return true;
    }
    assert(this->m_fitting_result.empty() || this->m_fitting_result.back().end_point_index < this->points.size());
    return false;
}

//Polylines to_polylines(const PolylinesOrArcs& polys_or_arcs) {
//    Polylines polys;
//    for (const PolylineOrArc& poly_or_arc : polys_or_arcs) {
//        polys.push_back(Polyline{ poly_or_arc.get_points() });
//    }
//    return polys;
//}

Polylines to_polylines(const ArcPolylines &arcpolys, coord_t deviation /*= 0*/)
{
    Polylines polys;
    for (const ArcPolyline &poly : arcpolys) {
        polys.push_back(poly.to_polyline(deviation));
    }
    return polys;
}

}
