// The MIT License (MIT)

// Copyright (c) 2013-2015 Danny Y., Rapptz

// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef SOL_TABLE_CORE_HPP
#define SOL_TABLE_CORE_HPP

#include "proxy.hpp"
#include "stack.hpp"
#include "function_types.hpp"
#include "usertype.hpp"

namespace sol {
namespace detail {
    struct global_overload_tag { } const global_overload;
} // detail

template <bool top_level>
class table_core : public reference {
    friend class state;
    template<typename T, typename Key, EnableIf<Not<std::is_arithmetic<Unqualified<Key>>>, Bool<top_level>> = 0>
    stack::get_return<T> single_get( Key&& key ) const {
        lua_getglobal( lua_state( ), &key[ 0 ] );
        stack::get_return<T> result = stack::pop<T>( lua_state( ) );
        return result;
    }

    template<typename T, typename Key, DisableIf<Not<std::is_arithmetic<Unqualified<Key>>>, Bool<top_level>> = 0>
    stack::get_return<T> single_get( Key&& key ) const {
        push( );
        stack::push( lua_state( ), std::forward<Key>( key ) );
        lua_gettable( lua_state( ), -2 );
        stack::get_return<T> result = stack::pop<T>( lua_state( ) );
        pop( );
        return result;
    }

    template<typename Keys, typename... Ret, std::size_t... I>
    stack::get_return<Ret...> tuple_get( types<Ret...>, indices<I...>, Keys&& keys ) const {
        return stack::get_return<Ret...>( single_get<Ret>( std::get<I>( keys ) )... );
    }

    template<typename Keys, typename Ret, std::size_t I>
    stack::get_return<Ret> tuple_get( types<Ret>, indices<I>, Keys&& keys ) const {
        return single_get<Ret>( std::get<I>( keys ) );
    }

#if SOL_LUA_VERSION < 502
    table_core( detail::global_overload_tag, const table_core<false>& reg ) noexcept : reference( reg.lua_state(), LUA_GLOBALSINDEX ) { }
#else
    table_core( detail::global_overload_tag, const table& reg ) noexcept : reference( reg.get<table>( LUA_RIDX_GLOBALS ) ) { }
#endif
public:
    table_core( ) noexcept : reference( ) { }
    table_core( const table_core<true>& global ) : reference( global ) { }
    table_core( lua_State* L, int index = -1 ) : reference( L, index ) {
        type_assert( L, index, type::table );
    }

    template<typename... Ret, typename... Keys>
    stack::get_return<Ret...> get( Keys&&... keys ) const {
        return tuple_get( types<Ret...>( ), build_indices<sizeof...( Ret )>( ), std::tie( keys... ) );
    }

    template<typename T, typename U>
    table_core& set( T&& key, U&& value ) {
        if ( top_level ) {
            stack::push( lua_state( ), std::forward<U>( value ) );
            lua_setglobal( lua_state( ), &key[0] );
        }
        else {
            push( );
            stack::push( lua_state( ), std::forward<T>( key ) );
            stack::push( lua_state( ), std::forward<U>( value ) );
            lua_settable( lua_state( ), -3 );
            pop( );
        }
        return *this;
    }

    template<typename T>
    SOL_DEPRECATED table_core& set_userdata( usertype<T>& user ) {
        return set_usertype( user );
    }

    template<typename Key, typename T>
    SOL_DEPRECATED table_core& set_userdata( Key&& key, usertype<T>& user ) {
        return set_usertype( std::forward<Key>( key ), user );
    }

    template<typename T>
    table_core& set_usertype( usertype<T>& user ) {
        return set_usertype( usertype_traits<T>::name, user );
    }

    template<typename Key, typename T>
    table_core& set_usertype( Key&& key, usertype<T>& user ) {
        if ( top_level ) {
            stack::push( lua_state( ), user );
            lua_setglobal( lua_state( ), &key[ 0 ] );
            pop( );
        }
        else {
            push( );
            stack::push( lua_state( ), std::forward<Key>( key ) );
            stack::push( lua_state( ), user );
            lua_settable( lua_state( ), -3 );
            pop( );
        }
        return *this;
    }

    template<typename Fx>
    void for_each( Fx&& fx ) const {
        push( );
        stack::push( lua_state( ), nil );
        while ( lua_next( this->lua_state( ), -2 ) ) {
            sol::object key( lua_state( ), -2 );
            sol::object value( lua_state( ), -1 );
            fx( key, value );
            lua_pop( lua_state( ), 1 );
        }
        pop( );
    }

    size_t size( ) const {
        push( );
        size_t result = lua_rawlen( lua_state( ), -1 );
        pop( );
        return result;
    }

    template<typename T>
    proxy<table_core, T> operator[]( T&& key ) {
        return proxy<table_core, T>( *this, std::forward<T>( key ) );
    }

    template<typename T>
    proxy<const table_core, T> operator[]( T&& key ) const {
        return proxy<const table_core, T>( *this, std::forward<T>( key ) );
    }

    void pop( int n = 1 ) const noexcept {
        lua_pop( lua_state( ), n );
    }

    template<typename... Args, typename R, typename Key>
    table_core& set_function( Key&& key, R fun_ptr( Args... ) ) {
        set_resolved_function( std::forward<Key>( key ), fun_ptr );
        return *this;
    }

    template<typename Sig, typename Key>
    table_core& set_function( Key&& key, Sig* fun_ptr ) {
        set_resolved_function( std::forward<Key>( key ), fun_ptr );
        return *this;
    }

    template<typename... Args, typename R, typename C, typename T, typename Key>
    table_core& set_function( Key&& key, R( C::*mem_ptr )( Args... ), T&& obj ) {
        set_resolved_function( std::forward<Key>( key ), mem_ptr, std::forward<T>( obj ) );
        return *this;
    }

    template<typename Sig, typename C, typename T, typename Key>
    table_core& set_function( Key&& key, Sig C::* mem_ptr, T&& obj ) {
        set_resolved_function( std::forward<Key>( key ), mem_ptr, std::forward<T>( obj ) );
        return *this;
    }

    template<typename... Sig, typename Fx, typename Key>
    table_core& set_function( Key&& key, Fx&& fx ) {
        set_fx( types<Sig...>( ), std::forward<Key>( key ), std::forward<Fx>( fx ) );
        return *this;
    }

private:
    template<typename R, typename... Args, typename Fx, typename Key, typename = typename std::result_of<Fx( Args... )>::type>
    void set_fx( types<R( Args... )>, Key&& key, Fx&& fx ) {
        set_resolved_function<R( Args... )>( std::forward<Key>( key ), std::forward<Fx>( fx ) );
    }

    template<typename Fx, typename Key>
    void set_fx( types<>, Key&& key, Fx&& fx ) {
        typedef Unqualified<Unwrap<Fx>> fx_t;
        typedef decltype( &fx_t::operator() ) Sig;
        set_fx( types<function_signature_t<Sig>>( ), std::forward<Key>( key ), std::forward<Fx>( fx ) );
    }

    template<typename... Sig, typename... Args, typename Key, EnableIf<Not<std::is_arithmetic<Unqualified<Key>>>, Bool<top_level>> = 0>
    void set_resolved_function( Key&& key, Args&&... args ) {
        stack::push<function_sig_t<Sig...>>( lua_state( ), std::forward<Args>( args )... );
        lua_setglobal( lua_state( ), &key[ 0 ] );
    }

    template<typename... Sig, typename... Args, typename Key, DisableIf<Not<std::is_arithmetic<Unqualified<Key>>>, Bool<top_level>> = 0>
    void set_resolved_function( Key&& key, Args&&... args ) {
        push( );
        int tabletarget = lua_gettop( lua_state( ) );
        stack::push<function_sig_t<Sig...>>( lua_state( ), std::forward<Args>( args )... );
        lua_setfield( lua_state( ), tabletarget, &key[ 0 ] );
        pop( );
    }
};
} // sol

#endif // SOL_TABLE_CORE_HPP