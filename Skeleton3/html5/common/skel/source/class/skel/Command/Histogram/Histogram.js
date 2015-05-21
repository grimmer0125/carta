/**
 * Container for histogram user preference commands.
 */
/*global mImport */
/*******************************************************************************
 * @ignore( mImport)
 ******************************************************************************/

qx.Class.define("skel.Command.Histogram.Histogram", {
    extend : skel.Command.CommandComposite,
    type : "singleton",

    /**
     * Constructor.
     */
    construct : function() {
        this.base( arguments, "Histogram", null );
        this.m_global = false;
        this.setEnabled( false );
        this.m_cmds = [];
        this.m_cmds.push( skel.Command.Histogram.Settings.getInstance());
        this.m_cmds.push( skel.Command.Histogram.Preferences.getInstance());
        this.setValue( this.m_cmds );
    },
    
    members : {
        
        /**
         * Tells children to reset their enabled statuses.
         */
        _resetEnabled : function( ){
            arguments.callee.base.apply(this, arguments);
            for ( var i = 0; i < this.m_cmds.length; i++ ){
                this.m_cmds[i]._resetEnabled( );
            }
        },
        
        /**
         * Update from the the server; shows or hides the setting.
         * @param obj {Object} the server side state.
         */
        setSettings : function( obj ){
            this.setCmdSettings( obj, this.m_cmds );
        }
    }
});